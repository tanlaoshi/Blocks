/*
 * MountOs.c — ROOT.CFG OsTarget + SecondaryReady（fs-msc-defer）
 *
 * 开机：读已挂 ESP 上 EFI/Blocks/ROOT.CFG，定启动卷（可跨盘）。
 * Files：SecondaryReady=0 时只展 OsDisk。
 */
#include "FileSystemPrivate.h"
#include "BootInfo.h"
#include "Gpt.h"
#include "VirtualFileSystem.h"
#include "SerialLog.h"

int gSecondaryReady;
int gOsVolIdx = -1;
UINT8 gOsDiskGuid[16];
int gOsDiskOk;

static int GuidEq16(const UINT8 *A, const UINT8 *B) {
    int i;

    for (i = 0; i < 16; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

static int VolIsEspName(const FS_VOLUME *V) {
    return V && V->Name[0] == 'E' && V->Name[1] == 'S' && V->Name[2] == 'P';
}

static int ParseRootCfg(const char *Buf, UINTN Len, UINT8 Disk[16], UINT8 Part[16]) {
    UINTN i = 0;
    int HaveDisk = 0;
    int HavePart = 0;

    while (i < Len) {
        char Key[8];
        char Val[40];
        UINTN k = 0;
        UINTN v = 0;

        while (i < Len && (Buf[i] == '\n' || Buf[i] == '\r' || Buf[i] == ' ')) {
            i++;
        }
        if (i >= Len) {
            break;
        }
        if (Buf[i] == '#') {
            while (i < Len && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        k = 0;
        while (i < Len && Buf[i] != '=' && Buf[i] != '\n' && k < sizeof(Key) - 1) {
            Key[k++] = Buf[i++];
        }
        Key[k] = 0;
        if (i >= Len || Buf[i] != '=') {
            while (i < Len && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        i++;
        v = 0;
        while (i < Len && Buf[i] != '\n' && Buf[i] != '\r' && v < sizeof(Val) - 1) {
            Val[v++] = Buf[i++];
        }
        Val[v] = 0;
        if (Key[0] == 'd' && Key[1] == 'i' && Key[2] == 's' && Key[3] == 'k' &&
            Key[4] == 0) {
            if (GptParseUuidStr(Val, Disk)) {
                HaveDisk = 1;
            }
        } else if (Key[0] == 'p' && Key[1] == 'a' && Key[2] == 'r' && Key[3] == 't' &&
                   Key[4] == 0) {
            if (GptParseUuidStr(Val, Part)) {
                HavePart = 1;
            }
        }
    }
    return HaveDisk && HavePart;
}

static int TryRootCfgFromVol(int VolIdx, UINT8 Disk[16], UINT8 Part[16]) {
    char Buf[256];
    UINTN Sz = 0;

    if (FileSystemActivate(VolIdx) != FAT_OK) {
        return 0;
    }
    if (gVirtualFileSystem.ReadFile("EFI\\Blocks\\ROOT.CFG", Buf, sizeof(Buf) - 1,
                                    &Sz) != FAT_OK) {
        Sz = 0;
        if (gVirtualFileSystem.ReadFile("EFI/Blocks/ROOT.CFG", Buf, sizeof(Buf) - 1,
                                        &Sz) != FAT_OK) {
            return 0;
        }
    }
    Buf[Sz] = 0;
    return ParseRootCfg(Buf, Sz, Disk, Part);
}

static int FindVolByDiskPart(const UINT8 Disk[16], const UINT8 Part[16]) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].DiskGuidOk || !gVols[i].PartGuidOk) {
            continue;
        }
        if (GuidEq16(gVols[i].DiskGuid, Disk) && GuidEq16(gVols[i].PartGuid, Part)) {
            return i;
        }
    }
    return -1;
}

void FsResolveOsTarget(int *BlocksVol) {
    int i;
    UINT8 Disk[16];
    UINT8 Part[16];
    int FromCfg = 0;

    gOsVolIdx = -1;
    gOsDiskOk = 0;

    for (i = 0; i < gVolCount; i++) {
        if (!VolIsEspName(&gVols[i])) {
            continue;
        }
        if (TryRootCfgFromVol(i, Disk, Part)) {
            FromCfg = 1;
            LogFs("Fs: ROOT.CFG loaded\n");
            break;
        }
    }

    if (FromCfg) {
        int Hit = FindVolByDiskPart(Disk, Part);

        for (i = 0; i < 16; i++) {
            gOsDiskGuid[i] = Disk[i];
        }
        gOsDiskOk = 1;
        if (Hit >= 0) {
            gOsVolIdx = Hit;
            if (gVols[Hit].HasBlocksId && BlocksVol) {
                *BlocksVol = Hit;
            }
            LogFs("Fs: OsTarget via ROOT.CFG\n");
            return;
        }
        /* ROOT.CFG 指向尚未挂上的卷（多半在 U 盘） */
        LogFs("Fs: OsTarget ROOT.CFG not mounted yet\n");
        return;
    }

    /* 无 ROOT.CFG：回退 BlocksVol / 启动盘 BLOCKS */
    if (BlocksVol && *BlocksVol >= 0) {
        gOsVolIdx = *BlocksVol;
    } else {
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].HasBlocksId) {
                gOsVolIdx = i;
                break;
            }
        }
    }
    if (gOsVolIdx >= 0 && gVols[gOsVolIdx].DiskGuidOk) {
        for (i = 0; i < 16; i++) {
            gOsDiskGuid[i] = gVols[gOsVolIdx].DiskGuid[i];
        }
        gOsDiskOk = 1;
    }
}

int FsOsVolumeMounted(void) {
    return gOsVolIdx >= 0;
}

int FsVolOnOsDisk(int Idx) {
    if (Idx < 0 || Idx >= gVolCount) {
        return 0;
    }
    if (!gOsDiskOk || !gVols[Idx].DiskGuidOk) {
        return 1; /* 无 GUID：不过滤 */
    }
    return GuidEq16(gVols[Idx].DiskGuid, gOsDiskGuid);
}

int FileSystemSecondaryVolsReady(void) {
    return gSecondaryReady ? 1 : 0;
}

int FileSystemVolOnOsDisk(int Idx) {
    return FsVolOnOsDisk(Idx);
}
