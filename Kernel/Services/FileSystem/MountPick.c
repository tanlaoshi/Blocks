/*
 * MountPick.c — 选默认卷 / 无名 ESP 命名（PR-F-fs-1）
 * 核心：Mount.c
 */
#include "FileSystemPrivate.h"
#include "VirtualFileSystem.h"
#include "SerialLog.h"

static void RenameEspIfLetterOnly(int i) {
    int NeedEspName = 0;

    gVols[i].ReadOnly = 1;
    if (!(gVols[i].Name[0] && gVols[i].Name[1])) {
        NeedEspName = 1;
    } else if (gVols[i].Name[0] == gVols[i].Letter && gVols[i].Name[1] == 0) {
        NeedEspName = 1;
    }
    if (!NeedEspName) {
        return;
    }
    /* 与 BLOCKS 一样：多份同名 ESP，不编号 */
    StringCopy(gVols[i].Name, FS_VOL_NAME_MAX, "ESP");
}

static void PickWithBlocksId(int BlocksVol) {
    int i;

    gDefaultVol = BlocksVol;
    /* 有 BLOCKS 卷时，其余 Block 卷视为启动/ESP：只读，名 ESP（仍可用 A:） */
    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].Ops && gVols[i].Ops->Synthetic) {
            continue;
        }
        if (!gVols[i].HasBlocksId) {
            RenameEspIfLetterOnly(i);
        }
    }
}

static void PickWithoutBlocksId(UINT8 *Tmp, UINTN TmpSz) {
    int i;
    int FatVol = -1;
    int PrefVol = -1;
    int MarkerVol = -1;
    UINTN Sz;

    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].Ops || gVols[i].Ops->Synthetic) {
            continue;
        }
        if (FatVol < 0) {
            FatVol = i;
        }
        if (!gVols[i].ReadOnly && PrefVol < 0) {
            PrefVol = i;
        }
        if (MarkerVol < 0 && FileSystemActivate(i) == FAT_OK) {
            if (gVirtualFileSystem.ReadFile("HELLO.ELF", Tmp, TmpSz, &Sz) == FAT_OK ||
                gVirtualFileSystem.ReadFile("Kernel.elf", Tmp, TmpSz, &Sz) == FAT_OK ||
                gVirtualFileSystem.ReadFile("BLOCKS.DB", Tmp, TmpSz, &Sz) == FAT_OK) {
                MarkerVol = i;
            }
        }
    }
    if (MarkerVol >= 0) {
        gDefaultVol = MarkerVol;
        StringCopy(gVols[MarkerVol].Name, FS_VOL_NAME_MAX, "BLOCKS");
        LogFs("Fs: Named BLOCKS by marker (No BLOCKS.ID)\n");
    } else if (PrefVol >= 0) {
        gDefaultVol = PrefVol;
    } else {
        gDefaultVol = (FatVol >= 0) ? FatVol : 0;
    }
    if (gDefaultVol >= 0 && gDefaultVol < gVolCount &&
        gVols[gDefaultVol].Name[0] == 'E' && gVols[gDefaultVol].Name[1] == 'S' &&
        gVols[gDefaultVol].Name[2] == 'P') {
        LogFs(
            "Fs: Default=ESP (No BLOCKS.ID; Put Rootfs On A FAT With BLOCKS.ID)\n");
    }
}

void FsMountPickDefault(int BlocksVol, UINT8 *Tmp, UINTN TmpSz) {
    if (BlocksVol >= 0) {
        PickWithBlocksId(BlocksVol);
    } else {
        PickWithoutBlocksId(Tmp, TmpSz);
    }
}
