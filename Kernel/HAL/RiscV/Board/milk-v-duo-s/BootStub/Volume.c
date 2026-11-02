/*
 * Volume.c — GPT 扫 FAT，认 BLOCKS.ID，读 Kernel.elf 到高地址缓冲
 */
#include "Block.h"
#include "BoardConfig.h"
#include "BootTypes.h"
#include "Fat.h"
#include "Gpt.h"

/* 真核装载前暂存 ELF；躲开 stub @0x83000000 */
#define BOOT_STUB_ELF_BUFFER 0x84000000ULL
#define BOOT_STUB_ELF_BUFFER_MAX (12u * 1024u * 1024u)

extern void BootStubSerialWrite(const char *Text);

int BootStubFindBlocksVolumeStart(UINT32 *OutStartLogicalBlockAddress) {
    GPT_FAT_PART Parts[GPT_MAX_FAT_PARTS];
    int Count;
    int Index;
    UINT8 Identity[64];
    UINTN Got;

    if (OutStartLogicalBlockAddress == 0) {
        return -1;
    }
    Count = GptFindAllFat(Parts, GPT_MAX_FAT_PARTS);
    if (Count <= 0) {
        BootStubSerialWrite("stub: no FAT partitions\n");
        return -1;
    }
    for (Index = 0; Index < Count; Index++) {
        if (FatInitialize(Parts[Index].StartLba) != FAT_OK) {
            continue;
        }
        Got = 0;
        if (FatReadFile("BLOCKS.ID", Identity, sizeof(Identity) - 1u, &Got) != FAT_OK) {
            continue;
        }
        Identity[Got < sizeof(Identity) ? Got : (sizeof(Identity) - 1u)] = 0;
        *OutStartLogicalBlockAddress = Parts[Index].StartLba;
        BootStubSerialWrite("stub: BLOCKS.ID ok\n");
        return 0;
    }
    BootStubSerialWrite("stub: BLOCKS.ID not found\n");
    return -1;
}

int BootStubReadKernelExecutable(void **OutImage, UINTN *OutSize) {
    UINT32 StartLogicalBlockAddress;
    UINTN Got = 0;
    int Result;

    if (OutImage == 0 || OutSize == 0) {
        return -1;
    }
    if (BootStubFindBlocksVolumeStart(&StartLogicalBlockAddress) != 0) {
        return -1;
    }
    if (FatInitialize(StartLogicalBlockAddress) != FAT_OK) {
        BootStubSerialWrite("stub: FatInitialize fail\n");
        return -1;
    }
    Result = FatReadFile("Kernel.elf", (void *)(UINTN)BOOT_STUB_ELF_BUFFER,
                         BOOT_STUB_ELF_BUFFER_MAX, &Got);
    if (Result != FAT_OK || Got < 64u) {
        BootStubSerialWrite("stub: Kernel.elf read fail\n");
        return -1;
    }
    *OutImage = (void *)(UINTN)BOOT_STUB_ELF_BUFFER;
    *OutSize = Got;
    BootStubSerialWrite("stub: Kernel.elf loaded\n");
    return 0;
}
