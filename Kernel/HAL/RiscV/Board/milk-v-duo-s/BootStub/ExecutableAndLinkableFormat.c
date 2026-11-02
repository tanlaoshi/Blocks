/*
 * ExecutableAndLinkableFormat.c — 把内存中的 ELF64 装到 PT_LOAD，跳 entry
 *
 * bootstub-elf-bound：装入须有上界与禁区——落在 DRAM 内、不盖 DTB/Stub、
 * phentsize≥56、memsz≥filesz、入口落在某个 PT_LOAD。否则拒跳（串口说明）。
 */
#include "BootTypes.h"
#include "BoardConfig.h"

#define ELF_MAGIC_0 0x7Fu
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELF_CLASS_64 2u
#define ELF_DATA_LITTLE 1u
#define ELF_PROGRAM_LOAD 1u
#define ELF64_PROGRAM_HEADER_BYTES 56u
/* 设备树预留窗：盖住 fdt_addr_r，避免 PT_LOAD 踩 DTB */
#define BOARD_DEVICE_TREE_RESERVE_BYTES (1ULL * 1024ULL * 1024ULL)

extern void BootStubSerialWrite(const char *Text);
extern char __boot_stub_end[];

typedef void (*KernelEntryFunction)(UINT64 HartId, UINT64 DeviceTreePhys);

static void MemoryCopy(UINT8 *Destination, const UINT8 *Source, UINTN Length) {
    UINTN Index;
    for (Index = 0; Index < Length; Index++) {
        Destination[Index] = Source[Index];
    }
}

static void MemorySet(UINT8 *Destination, UINT8 Value, UINTN Length) {
    UINTN Index;
    for (Index = 0; Index < Length; Index++) {
        Destination[Index] = Value;
    }
}

static int RangeOverlaps(UINT64 Start, UINT64 Length, UINT64 ForbidStart,
                         UINT64 ForbidLength) {
    if (Length == 0u || ForbidLength == 0u) {
        return 0;
    }
    return Start < ForbidStart + ForbidLength && ForbidStart < Start + Length;
}

static UINT64 BootStubImageBytes(void) {
    UINT64 End = (UINT64)(UINTN)(void *)__boot_stub_end;
    if (End <= BOARD_BOOT_STUB_LOAD) {
        return 2ULL * 1024ULL * 1024ULL;
    }
    return End - BOARD_BOOT_STUB_LOAD;
}

/*
 * 允许装入：整段在 DRAM 内；不与 DTB 预留窗、BootStub 自身重叠。
 */
static int LoadRangeAllowed(UINT64 VirtualAddress, UINT64 MemorySize) {
    UINT64 End;
    UINT64 RamEnd = BOARD_RAM_BASE + BOARD_RAM_SIZE;

    if (MemorySize == 0u) {
        return 0;
    }
    if (VirtualAddress < BOARD_RAM_BASE || VirtualAddress < BOARD_KERNEL_LOAD) {
        return 0;
    }
    End = VirtualAddress + MemorySize;
    if (End < VirtualAddress || End > RamEnd) {
        return 0;
    }
    if (RangeOverlaps(VirtualAddress, MemorySize, BOARD_DTB_LOAD,
                      BOARD_DEVICE_TREE_RESERVE_BYTES)) {
        return 0;
    }
    if (RangeOverlaps(VirtualAddress, MemorySize, BOARD_BOOT_STUB_LOAD,
                      BootStubImageBytes())) {
        return 0;
    }
    return 1;
}

static int EntryFallsInLoad(UINT64 Entry, UINT64 VirtualAddress, UINT64 MemorySize) {
    return Entry >= VirtualAddress && Entry < VirtualAddress + MemorySize;
}

static void Reject(const char *Why) {
    BootStubSerialWrite("BootStub: ELF reject ");
    BootStubSerialWrite(Why);
    BootStubSerialWrite("\n");
}

int BootStubLoadExecutableAndJump(const void *Image, UINTN ImageSize, UINT64 HartId,
                                  UINT64 DeviceTreePhys) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    UINT64 Entry;
    UINT64 ProgramHeaderOffset;
    UINT16 ProgramHeaderEntrySize;
    UINT16 ProgramHeaderCount;
    UINT16 Index;
    int EntryCovered = 0;

    if (Image == 0 || ImageSize < 64u) {
        Reject("tiny");
        return -1;
    }
    if (Bytes[0] != ELF_MAGIC_0 || Bytes[1] != ELF_MAGIC_1 || Bytes[2] != ELF_MAGIC_2 ||
        Bytes[3] != ELF_MAGIC_3) {
        Reject("magic");
        return -1;
    }
    if (Bytes[4] != ELF_CLASS_64 || Bytes[5] != ELF_DATA_LITTLE) {
        Reject("class");
        return -1;
    }

    Entry = *(const UINT64 *)(const void *)(Bytes + 24);
    ProgramHeaderOffset = *(const UINT64 *)(const void *)(Bytes + 32);
    ProgramHeaderEntrySize = *(const UINT16 *)(const void *)(Bytes + 54);
    ProgramHeaderCount = *(const UINT16 *)(const void *)(Bytes + 56);

    if (ProgramHeaderEntrySize < ELF64_PROGRAM_HEADER_BYTES) {
        Reject("phentsize");
        return -1;
    }
    if (ProgramHeaderOffset + (UINT64)ProgramHeaderCount * ProgramHeaderEntrySize >
        ImageSize) {
        Reject("ph-table");
        return -1;
    }

    for (Index = 0; Index < ProgramHeaderCount; Index++) {
        const UINT8 *Header =
            Bytes + ProgramHeaderOffset + (UINT64)Index * ProgramHeaderEntrySize;
        UINT32 Type = *(const UINT32 *)(const void *)Header;
        UINT64 Offset;
        UINT64 VirtualAddress;
        UINT64 FileSize;
        UINT64 MemorySize;

        if (Type != ELF_PROGRAM_LOAD) {
            continue;
        }
        Offset = *(const UINT64 *)(const void *)(Header + 8);
        VirtualAddress = *(const UINT64 *)(const void *)(Header + 16);
        FileSize = *(const UINT64 *)(const void *)(Header + 32);
        MemorySize = *(const UINT64 *)(const void *)(Header + 40);
        if (MemorySize < FileSize) {
            Reject("memsz");
            return -1;
        }
        if (Offset + FileSize < Offset || Offset + FileSize > ImageSize) {
            Reject("filesz");
            return -1;
        }
        if (!LoadRangeAllowed(VirtualAddress, MemorySize)) {
            Reject("range");
            return -1;
        }
        if (EntryFallsInLoad(Entry, VirtualAddress, MemorySize)) {
            EntryCovered = 1;
        }
        MemoryCopy((UINT8 *)(UINTN)VirtualAddress, Bytes + Offset, (UINTN)FileSize);
        if (MemorySize > FileSize) {
            MemorySet((UINT8 *)(UINTN)(VirtualAddress + FileSize), 0,
                      (UINTN)(MemorySize - FileSize));
        }
    }

    if (!EntryCovered) {
        Reject("entry");
        return -1;
    }

    ((KernelEntryFunction)(UINTN)Entry)(HartId, DeviceTreePhys);
    return -1; /* 不返回 */
}
