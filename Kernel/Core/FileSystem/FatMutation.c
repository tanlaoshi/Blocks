/*
 * FatMutation.c — K24：根目录 write / mkdir / rm（只动根；8.3）
 *
 * QEMU fat:rw(vvfat)：删项只标 0xE5，不释放簇链（现网同策）。
 */
#include "FatFile.h"
#include "FatVolume.h"
#include "Volume.h"
#include "HalBlock.h"

#define WRITE_MAX 4096u

static int OpenVol(FAT_VOLUME *V) {
    if (VolumeOpenActive(V) == 0) {
        return 0;
    }
    return FatVolumeOpen(V);
}

static int VolReadOnly(void) {
    const VOLUME *V = VolumeGet(VolumeActiveIndex());
    return (V != 0 && V->ReadOnly) ? 1 : 0;
}

static int Resolve83(const char *Path, char Name83[11]) {
    const char *Rel = Path;
    if (Path == 0) {
        return -1;
    }
    (void)VolumeResolve(Path, &Rel);
    if (Rel == 0 || Rel[0] == 0) {
        return -1;
    }
    return FatPathTo83(Rel, Name83);
}


typedef struct {
    const char *Want83;
    int Have;
    int Free;
    UINT32 EntLba;
    UINT32 EntOff;
    UINT32 FreeLba;
    UINT32 FreeOff;
    UINT8 Ent[32];
} SLOT;

static int MemEq(const UINT8 *A, const char *B, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        if (A[i] != (UINT8)B[i]) {
            return 0;
        }
    }
    return 1;
}

static void Zero(UINT8 *P, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        P[i] = 0;
    }
}

static int ScanSec(UINT8 *Sec, UINT32 Lba, SLOT *S) {
    UINTN Off;
    for (Off = 0; Off < FAT_SECTOR; Off += 32u) {
        if (Sec[Off] == 0x00u) {
            if (!S->Free) {
                S->Free = 1;
                S->FreeLba = Lba;
                S->FreeOff = (UINT32)Off;
            }
            return 1; /* 目录尾 */
        }
        if (Sec[Off] == 0xE5u) {
            if (!S->Free) {
                S->Free = 1;
                S->FreeLba = Lba;
                S->FreeOff = (UINT32)Off;
            }
            continue;
        }
        if ((Sec[Off + 11] & 0x08u) != 0 || (Sec[Off + 11] & 0x0Fu) == 0x0Fu) {
            continue;
        }
        if (MemEq(Sec + Off, S->Want83, 11)) {
            S->Have = 1;
            S->EntLba = Lba;
            S->EntOff = (UINT32)Off;
            {
                UINTN i;
                for (i = 0; i < 32; i++) {
                    S->Ent[i] = Sec[Off + i];
                }
            }
            return 1;
        }
    }
    return 0;
}

static int FindSlot(FAT_VOLUME *V, const char Name83[11], SLOT *S) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 i;
    UINT32 Guard;

    S->Want83 = Name83;
    S->Have = 0;
    S->Free = 0;
    if (V->FatBits != 32) {
        for (i = 0; i < V->RootSecs && i < 128u; i++) {
            if (HalBlockRead(V->RootLba + i, Sec, 1) != 0) {
                return -1;
            }
            if (ScanSec(Sec, V->RootLba + i, S)) {
                return 0;
            }
        }
        return 0;
    }
    {
        UINT32 Clus = V->RootClus;
        for (Guard = 0; Guard < 64u && Clus >= 2u; Guard++) {
            UINT32 Lba = FatVolumeClusLba(V, Clus);
            UINT8 s;
            for (s = 0; s < V->Spc; s++) {
                if (HalBlockRead(Lba + s, Sec, 1) != 0) {
                    return -1;
                }
                if (ScanSec(Sec, Lba + s, S)) {
                    return 0;
                }
            }
            Clus = FatVolumeNext(V, Clus);
            if (Clus < 2u || Clus >= 0x0FFFFFF8u) {
                break;
            }
        }
    }
    return 0;
}

static int PutEnt(UINT32 Lba, UINT32 Off, const UINT8 Ent[32]) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 i;
    if (HalBlockRead(Lba, Sec, 1) != 0) {
        return -1;
    }
    for (i = 0; i < 32u; i++) {
        Sec[Off + i] = Ent[i];
    }
    return HalBlockWrite(Lba, Sec, 1);
}

static void FillEnt(UINT8 Ent[32], const char Name83[11], UINT8 Attr, UINT32 Clus,
                    UINT32 Size) {
    UINT32 i;
    Zero(Ent, 32);
    for (i = 0; i < 11u; i++) {
        Ent[i] = (UINT8)Name83[i];
    }
    Ent[11] = Attr;
    FatWr16(Ent + 20, (UINT16)((Clus >> 16) & 0xFFFFu));
    FatWr16(Ent + 26, (UINT16)(Clus & 0xFFFFu));
    FatWr32(Ent + 28, Size);
}

int FatFileWritePath(const char *Path, const void *Buf, UINT32 Len) {
    FAT_VOLUME V;
    char Name83[11];
    SLOT S;
    UINT32 Clus;
    UINT32 StoreLen = Len;
    const UINT8 *Src = (const UINT8 *)Buf;
    static const UINT8 Pad[1] = { 0 };
    UINT8 Ent[32];

    if (Path == 0 || (Buf == 0 && Len > 0) || Len > WRITE_MAX) {
        return -1;
    }
    if (Resolve83(Path, Name83) != 0 || OpenVol(&V) != 0) {
        return -1;
    }
    if (VolReadOnly()) {
        return -1;
    }
    /* vvfat：空文件仍写 1 字节，否则宿主侧常不现 */
    if (StoreLen == 0) {
        Src = Pad;
        StoreLen = 1;
    }
    if (FindSlot(&V, Name83, &S) != 0) {
        return -1;
    }
    if (S.Have) {
        if ((S.Ent[11] & FAT_ATTR_DIR) != 0) {
            return -1;
        }
        Clus = ((UINT32)FatRd16(S.Ent + 20) << 16) | FatRd16(S.Ent + 26);
        if (Clus < 2u) {
            Clus = FatVolumeAlloc(&V);
            if (Clus < 2u) {
                return -1;
            }
        }
        if (FatVolumeWriteCluster(&V, Clus, Src, StoreLen) != 0) {
            return -1;
        }
        FillEnt(Ent, Name83, FAT_ATTR_ARCH, Clus, StoreLen);
        return PutEnt(S.EntLba, S.EntOff, Ent);
    }
    if (!S.Free) {
        return -1;
    }
    Clus = FatVolumeAlloc(&V);
    if (Clus < 2u) {
        return -1;
    }
    if (FatVolumeWriteCluster(&V, Clus, Src, StoreLen) != 0) {
        return -1;
    }
    FillEnt(Ent, Name83, FAT_ATTR_ARCH, Clus, StoreLen);
    return PutEnt(S.FreeLba, S.FreeOff, Ent);
}

int FatMkdirPath(const char *Path) {
    FAT_VOLUME V;
    char Name83[11];
    SLOT S;
    UINT32 Clus;
    UINT8 Ent[32];
    UINT8 Dir[FAT_SECTOR];
    UINT32 i;

    if (Resolve83(Path, Name83) != 0 || OpenVol(&V) != 0) {
        return -1;
    }
    if (VolReadOnly()) {
        return -1;
    }
    if (FindSlot(&V, Name83, &S) != 0) {
        return -1;
    }
    if (S.Have) {
        return -1;
    }
    if (!S.Free) {
        return -1;
    }
    Clus = FatVolumeAlloc(&V);
    if (Clus < 2u) {
        return -1;
    }
    Zero(Dir, FAT_SECTOR);
    /* "." */
    for (i = 0; i < 11u; i++) {
        Dir[i] = (UINT8)' ';
    }
    Dir[0] = '.';
    Dir[11] = FAT_ATTR_DIR;
    FatWr16(Dir + 26, (UINT16)(Clus & 0xFFFFu));
    FatWr16(Dir + 20, (UINT16)((Clus >> 16) & 0xFFFFu));
    /* ".." → 根 */
    for (i = 0; i < 11u; i++) {
        Dir[32 + i] = (UINT8)' ';
    }
    Dir[32] = '.';
    Dir[33] = '.';
    Dir[32 + 11] = FAT_ATTR_DIR;
    if (V.FatBits == 32) {
        FatWr16(Dir + 32 + 26, (UINT16)(V.RootClus & 0xFFFFu));
        FatWr16(Dir + 32 + 20, (UINT16)((V.RootClus >> 16) & 0xFFFFu));
    }
    if (FatVolumeWriteCluster(&V, Clus, Dir, FAT_SECTOR) != 0) {
        return -1;
    }
    FillEnt(Ent, Name83, FAT_ATTR_DIR, Clus, 0);
    return PutEnt(S.FreeLba, S.FreeOff, Ent);
}

int FatRmPath(const char *Path) {
    FAT_VOLUME V;
    char Name83[11];
    SLOT S;
    UINT8 Sec[FAT_SECTOR];

    if (Resolve83(Path, Name83) != 0 || OpenVol(&V) != 0) {
        return -1;
    }
    if (VolReadOnly()) {
        return -1;
    }
    if (FindSlot(&V, Name83, &S) != 0 || !S.Have) {
        return -1;
    }
    if (HalBlockRead(S.EntLba, Sec, 1) != 0) {
        return -1;
    }
    Sec[S.EntOff] = 0xE5u;
    return HalBlockWrite(S.EntLba, Sec, 1);
}

int FatRenamePath(const char *OldPath, const char *NewPath) {
    FAT_VOLUME V;
    char Old83[11];
    char New83[11];
    const char *Rold;
    const char *Rnew;
    int VolIdx;
    SLOT Sold;
    SLOT Snew;
    UINT8 Ent[32];
    UINT32 i;
    int Same;

    if (OldPath == 0 || NewPath == 0) {
        return -1;
    }
    if (VolumeResolve(OldPath, &Rold) != 0 || Rold == 0 || Rold[0] == 0) {
        return -1;
    }
    VolIdx = VolumeActiveIndex();
    if (FatPathTo83(Rold, Old83) != 0) {
        return -1;
    }
    if (VolumeResolve(NewPath, &Rnew) != 0 || Rnew == 0 || Rnew[0] == 0) {
        return -1;
    }
    if (VolumeActiveIndex() != VolIdx) {
        return -1; /* 跨卷不做 */
    }
    if (FatPathTo83(Rnew, New83) != 0) {
        return -1;
    }
    Same = 1;
    for (i = 0; i < 11u; i++) {
        if (Old83[i] != New83[i]) {
            Same = 0;
            break;
        }
    }
    if (Same) {
        return 0;
    }
    if (OpenVol(&V) != 0 || VolReadOnly()) {
        return -1;
    }
    if (FindSlot(&V, Old83, &Sold) != 0 || !Sold.Have) {
        return -1;
    }
    if (FindSlot(&V, New83, &Snew) != 0) {
        return -1;
    }
    if (Snew.Have) {
        return -1;
    }
    for (i = 0; i < 32u; i++) {
        Ent[i] = Sold.Ent[i];
    }
    for (i = 0; i < 11u; i++) {
        Ent[i] = (UINT8)New83[i];
    }
    return PutEnt(Sold.EntLba, Sold.EntOff, Ent);
}
