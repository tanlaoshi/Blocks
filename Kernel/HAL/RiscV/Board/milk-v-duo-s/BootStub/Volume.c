/*
 * Volume.c — GPT 扫 FAT，认 BLOCKS.ID（装核见 SelectKernelImage.c）
 */
#include "Block.h"
#include "BoardConfig.h"
#include "BootTypes.h"
#include "Fat.h"
#include "Gpt.h"

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
