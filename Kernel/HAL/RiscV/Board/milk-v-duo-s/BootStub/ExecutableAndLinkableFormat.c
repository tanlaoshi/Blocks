/*
 * ExecutableAndLinkableFormat.c — 把内存中的 ELF64 装到 PT_LOAD，跳 entry
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

int BootStubLoadExecutableAndJump(const void *Image, UINTN ImageSize, UINT64 HartId,
                                  UINT64 DeviceTreePhys) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    UINT64 Entry;
    UINT64 ProgramHeaderOffset;
    UINT16 ProgramHeaderEntrySize;
    UINT16 ProgramHeaderCount;
    UINT16 Index;

    if (Image == 0 || ImageSize < 64u) {
        return -1;
    }
    if (Bytes[0] != ELF_MAGIC_0 || Bytes[1] != ELF_MAGIC_1 || Bytes[2] != ELF_MAGIC_2 ||
        Bytes[3] != ELF_MAGIC_3) {
        return -1;
    }
    if (Bytes[4] != ELF_CLASS_64 || Bytes[5] != ELF_DATA_LITTLE) {
        return -1;
    }

    Entry = *(const UINT64 *)(const void *)(Bytes + 24);
    ProgramHeaderOffset = *(const UINT64 *)(const void *)(Bytes + 32);
    ProgramHeaderEntrySize = *(const UINT16 *)(const void *)(Bytes + 54);
    ProgramHeaderCount = *(const UINT16 *)(const void *)(Bytes + 56);

    if (ProgramHeaderOffset + (UINT64)ProgramHeaderCount * ProgramHeaderEntrySize > ImageSize) {
        return -1;
    }

    for (Index = 0; Index < ProgramHeaderCount; Index++) {
        const UINT8 *Header = Bytes + ProgramHeaderOffset + (UINT64)Index * ProgramHeaderEntrySize;
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
        if (Offset + FileSize > ImageSize) {
            return -1;
        }
        if (VirtualAddress < BOARD_KERNEL_LOAD) {
            return -1;
        }
        MemoryCopy((UINT8 *)(UINTN)VirtualAddress, Bytes + Offset, (UINTN)FileSize);
        if (MemorySize > FileSize) {
            MemorySet((UINT8 *)(UINTN)(VirtualAddress + FileSize), 0,
                      (UINTN)(MemorySize - FileSize));
        }
    }

    ((KernelEntryFunction)(UINTN)Entry)(HartId, DeviceTreePhys);
    return -1; /* 不返回 */
}
