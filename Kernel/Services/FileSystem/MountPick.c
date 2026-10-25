/*
 * MountPick.c — 选默认卷 / 无名 ESP 命名（PR-F-fs-1）
 * 核心：Mount.c
 */
#include "FileSystemPrivate.h"
#include "VirtualFileSystem.h"
#include "SerialLog.h"
#include "BootHandoff.h"

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

/*
 * 无 BLOCKS.ID 时的启发式。
 * bootfs 只留固件（boot.scr/u-boot/config）；Kernel.elf 在 BLOCKS（对齐 X64）。
 * 勿用 Kernel.elf 判 boot —— 它应在系统卷上。
 */
static int VolLooksLikeBootFirmware(UINT8 *Tmp, UINTN TmpSz) {
    UINTN Sz;

    if (gVirtualFileSystem.ReadFile("boot.scr", Tmp, TmpSz, &Sz) == FAT_OK ||
        gVirtualFileSystem.ReadFile("config.txt", Tmp, TmpSz, &Sz) == FAT_OK ||
        gVirtualFileSystem.ReadFile("u-boot.bin", Tmp, TmpSz, &Sz) == FAT_OK) {
        return 1;
    }
    return 0;
}

static int VolLooksLikeBlocksRoot(UINT8 *Tmp, UINTN TmpSz) {
    UINTN Sz;

    if (gVirtualFileSystem.ReadFile("HELLO.ELF", Tmp, TmpSz, &Sz) == FAT_OK ||
        gVirtualFileSystem.ReadFile("BLOCKS.DB", Tmp, TmpSz, &Sz) == FAT_OK ||
        gVirtualFileSystem.ReadFile("TOYOS.ID", Tmp, TmpSz, &Sz) == FAT_OK ||
        gVirtualFileSystem.ReadFile("Assets/Fonts/CJK.TTF", Tmp, TmpSz, &Sz) ==
            FAT_OK) {
        return 1;
    }
    return 0;
}

static void PickWithoutBlocksId(UINT8 *Tmp, UINTN TmpSz) {
    int i;
    int FatVol = -1;
    int PrefVol = -1;
    int MarkerVol = -1;
    int BootVol = -1;

    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].Ops || gVols[i].Ops->Synthetic) {
            continue;
        }
        if (FatVol < 0) {
            FatVol = i;
        }
        if (FileSystemActivate(i) != FAT_OK) {
            continue;
        }
        if (VolLooksLikeBootFirmware(Tmp, TmpSz)) {
            /* MBR bootfs：非 0xEF 也当 ESP，勿留给 BLOCKS 启发式 */
            RenameEspIfLetterOnly(i);
            if (BootVol < 0) {
                BootVol = i;
            }
            continue;
        }
        if (!gVols[i].ReadOnly && PrefVol < 0) {
            PrefVol = i;
        }
        if (MarkerVol < 0 && VolLooksLikeBlocksRoot(Tmp, TmpSz)) {
            MarkerVol = i;
        }
    }
    if (MarkerVol >= 0) {
        gDefaultVol = MarkerVol;
        StringCopy(gVols[MarkerVol].Name, FS_VOL_NAME_MAX, "BLOCKS");
        LogFs("Fs: Named BLOCKS by root marker (No BLOCKS.ID)\n");
    } else if (PrefVol >= 0) {
        gDefaultVol = PrefVol;
    } else if (FatVol >= 0 && FatVol != BootVol) {
        gDefaultVol = FatVol;
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
    const BOOT_HANDOFF *Info = BootHandoffGet();

    /*
     * U 盘起、启动盘 BLOCKS 尚未 MSC 进表：禁止选旁路盘上的 BLOCKS
     *（含「扫到 Kernel.elf/BLOCKS.DB 就当地默认」）。暂用 ESP/RES。
     */
    if (Info && Info->BootDiskGuidOk && FsBootDiskHasBlocksId() == 0) {
        int i;
        int Esp = -1;
        int Res = -1;

        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].Ops && gVols[i].Ops->Synthetic) {
                Res = i;
                continue;
            }
            if (gVols[i].HasBlocksId) {
                continue;
            }
            RenameEspIfLetterOnly(i);
            if (Esp < 0) {
                Esp = i;
            }
        }
        if (Esp >= 0) {
            gDefaultVol = Esp;
        } else if (Res >= 0) {
            gDefaultVol = Res;
        } else {
            gDefaultVol = 0;
        }
        LogFs("Fs: Default=ESP/RES (boot-disk BLOCKS pending; skip bypass)\n");
        return;
    }
    if (BlocksVol >= 0) {
        PickWithBlocksId(BlocksVol);
    } else {
        PickWithoutBlocksId(Tmp, TmpSz);
    }
}
