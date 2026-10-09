/*
 * FatMut.c — K24：根目录 write / mkdir / rm（只动根；8.3）
 *
 * QEMU fat:rw(vvfat)：删项只标 0xE5，不释放簇链（现网同策）。
 */
#include "FatFile.h"
#include "FatVol.h"
#include "HalBlock.h"

#define WRITE_MAX 4096u

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

static int FindSlot(FAT_VOL *V, const char Name83[11], SLOT *S) {
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
            UINT32 Lba = FatVolClusLba(V, Clus);
            UINT8 s;
            for (s = 0; s < V->Spc; s++) {
                if (HalBlockRead(Lba + s, Sec, 1) != 0) {
                    return -1;
                }
                if (ScanSec(Sec, Lba + s, S)) {
                    return 0;
                }
            }
            Clus = FatVolNext(V, Clus);
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
    FAT_VOL V;
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
    if (FatPathTo83(Path, Name83) != 0 || FatVolOpen(&V) != 0) {
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
            Clus = FatVolAlloc(&V);
            if (Clus < 2u) {
                return -1;
            }
        }
        if (FatVolWriteCluster(&V, Clus, Src, StoreLen) != 0) {
            return -1;
        }
        FillEnt(Ent, Name83, FAT_ATTR_ARCH, Clus, StoreLen);
        return PutEnt(S.EntLba, S.EntOff, Ent);
    }
    if (!S.Free) {
        return -1;
    }
    Clus = FatVolAlloc(&V);
    if (Clus < 2u) {
        return -1;
    }
    if (FatVolWriteCluster(&V, Clus, Src, StoreLen) != 0) {
        return -1;
    }
    FillEnt(Ent, Name83, FAT_ATTR_ARCH, Clus, StoreLen);
    return PutEnt(S.FreeLba, S.FreeOff, Ent);
}

int FatMkdirPath(const char *Path) {
    FAT_VOL V;
    char Name83[11];
    SLOT S;
    UINT32 Clus;
    UINT8 Ent[32];
    UINT8 Dir[FAT_SECTOR];
    UINT32 i;

    if (FatPathTo83(Path, Name83) != 0 || FatVolOpen(&V) != 0) {
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
    Clus = FatVolAlloc(&V);
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
    if (FatVolWriteCluster(&V, Clus, Dir, FAT_SECTOR) != 0) {
        return -1;
    }
    FillEnt(Ent, Name83, FAT_ATTR_DIR, Clus, 0);
    return PutEnt(S.FreeLba, S.FreeOff, Ent);
}

int FatRmPath(const char *Path) {
    FAT_VOL V;
    char Name83[11];
    SLOT S;
    UINT8 Sec[FAT_SECTOR];

    if (FatPathTo83(Path, Name83) != 0 || FatVolOpen(&V) != 0) {
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
