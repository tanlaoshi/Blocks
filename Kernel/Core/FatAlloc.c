/*
 * FatAlloc.c — K24：FAT 表写项 / 分配簇 / 写簇数据
 */
#include "FatVol.h"
#include "HalBlock.h"

static UINT32 Eoc(const FAT_VOL *V) {
    return (V->FatBits == 32) ? 0x0FFFFFFFu : 0xFFFFu;
}

int FatVolSetNext(FAT_VOL *V, UINT32 Clus, UINT32 Next) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 EntPerSec;
    UINT32 Rel;
    UINT32 Off;
    UINT8 f;

    if (V == 0 || Clus < 2u) {
        return -1;
    }
    if (V->FatBits == 32) {
        EntPerSec = FAT_SECTOR / 4u;
        Rel = Clus / EntPerSec;
        Off = (Clus % EntPerSec) * 4u;
    } else {
        EntPerSec = FAT_SECTOR / 2u;
        Rel = Clus / EntPerSec;
        Off = (Clus % EntPerSec) * 2u;
    }
    if (HalBlockRead(V->FatLba + Rel, Sec, 1) != 0) {
        return -1;
    }
    if (V->FatBits == 32) {
        FatWr32(Sec + Off, Next & 0x0FFFFFFFu);
    } else {
        FatWr16(Sec + Off, (UINT16)Next);
    }
    for (f = 0; f < V->Nfats; f++) {
        if (HalBlockWrite(V->FatLba + (UINT32)f * V->FatSz + Rel, Sec, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

UINT32 FatVolAlloc(FAT_VOL *V) {
    UINT32 Max;
    UINT32 C;

    if (V == 0) {
        return 0;
    }
    if (V->FatBits == 32) {
        Max = V->FatSz * (FAT_SECTOR / 4u);
    } else {
        Max = V->FatSz * (FAT_SECTOR / 2u);
    }
    if (Max > 8192u) {
        Max = 8192u;
    }
    for (C = 2u; C < Max; C++) {
        if (FatVolNext(V, C) == 0) {
            if (FatVolSetNext(V, C, Eoc(V)) == 0) {
                return C;
            }
            return 0;
        }
    }
    return 0;
}

UINT32 FatVolClusLba(const FAT_VOL *V, UINT32 Clus) {
    if (V == 0 || Clus < 2u) {
        return 0;
    }
    return V->DataLba + (Clus - 2u) * (UINT32)V->Spc;
}

int FatVolWriteCluster(FAT_VOL *V, UINT32 Clus, const UINT8 *Data, UINT32 Len) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 Lba;
    UINT8 s;
    UINT32 Off = 0;
    UINT32 i;

    if (V == 0 || Clus < 2u) {
        return -1;
    }
    Lba = FatVolClusLba(V, Clus);
    if (Lba == 0) {
        return -1;
    }
    for (s = 0; s < V->Spc; s++) {
        for (i = 0; i < FAT_SECTOR; i++) {
            Sec[i] = 0;
        }
        if (Data != 0 && Off < Len) {
            UINT32 Chunk = Len - Off;
            if (Chunk > FAT_SECTOR) {
                Chunk = FAT_SECTOR;
            }
            for (i = 0; i < Chunk; i++) {
                Sec[i] = Data[Off + i];
            }
            Off += Chunk;
        }
        if (HalBlockWrite(Lba + s, Sec, 1) != 0) {
            return -1;
        }
    }
    return 0;
}
