/*
 * Tls.c — 主 ELF PT_TLS 装入（Variant II：TP 在块尾，对象负偏移）
 *
 * x86_64：FS=TP，%fs:0 仍留给 Tid（thr-2）；.tdata 在 TP-memsz … TP。
 */
#include "ElfPrivate.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "SerialLog.h"

/* SO 基址之下 64KiB：避开 PT_LOAD / 堆顶（BrkMax==SoBase） */
#define ELF_TLS_GAP_BELOW_SO 0x10000ULL
#define ELF_TLS_TCB_SIZE     8u

int ElfInstallMainTls(VIRTUAL_ADDRESS_SPACE *Space, const void *Image, UINTN Size,
                      UINT64 *OutTlsBase) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;
    const Elf64_Phdr *Ph;
    UINT16 Pn;
    UINT16 i;
    const Elf64_Phdr *Tls = 0;
    UINT64 MemSize;
    UINT64 Align;
    UINT64 Total;
    UINT64 MapStart;
    UINT64 MapEnd;
    UINT64 Virt;
    UINT64 Zero = 0;

    if (OutTlsBase) {
        *OutTlsBase = 0;
    }
    if (!Space || !Image || !OutTlsBase) {
        return -1;
    }
    if (!ElfHeaderOk(Hdr, Size, ET_EXEC)) {
        return -1;
    }
    if (ElfPhdrs(Bytes, Size, Hdr, &Ph, &Pn) != 0) {
        return -1;
    }
    for (i = 0; i < Pn; i++) {
        if (Ph[i].p_type == PT_TLS) {
            Tls = &Ph[i];
            break;
        }
    }
    if (!Tls || Tls->p_memsz == 0) {
        return 0;
    }

    Align = Tls->p_align ? Tls->p_align : 1u;
    MemSize = ElfAlignUp(Tls->p_memsz, Align);
    Total = MemSize + ELF_TLS_TCB_SIZE;
    MapStart = USER_SO_BASE - ELF_TLS_GAP_BELOW_SO;
    MapStart &= ~(UINT64)(PAGE_SIZE - 1);
    MapEnd = ElfAlignUp(MapStart + Total, PAGE_SIZE);

    for (Virt = MapStart; Virt < MapEnd; Virt += PAGE_SIZE) {
        void *Page = PhysicalMemoryAllocatePage();
        UINT64 Flags = PTE_PRESENT | PTE_USER | PTE_WRITABLE;

        if (!Page) {
            LogMem("elf: TLS alloc failed\n");
            return -1;
        }
        ElfZeroMemory(Page, PAGE_SIZE);
        if (VirtualMemorySpaceMapPage(Space, Virt, (UINT64)(UINTN)Page, Flags) != 0) {
            PhysicalMemoryFreePage(Page);
            LogMem("elf: TLS map failed\n");
            return -1;
        }
    }

    if (Tls->p_filesz > 0) {
        if (Tls->p_offset + Tls->p_filesz > Size) {
            LogMem("elf: TLS filesz oob\n");
            return -1;
        }
        if (VirtualMemoryCopyToSpace(Space, MapStart, Bytes + Tls->p_offset,
                                     (UINTN)Tls->p_filesz) < 0) {
            LogMem("elf: TLS copy tdata failed\n");
            return -1;
        }
    }
    /* tbss：filesz..memsz 已由页清零；TCB 槽先写 0，EnsureTls 再填 Tid */
    if (VirtualMemoryCopyToSpace(Space, MapStart + MemSize, &Zero, sizeof(Zero)) < 0) {
        LogMem("elf: TLS tcb zero failed\n");
        return -1;
    }

    *OutTlsBase = MapStart + MemSize; /* TP：负偏移进 .tdata；+0 为 Tid */
    return 0;
}
