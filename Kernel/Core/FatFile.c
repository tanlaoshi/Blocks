/*
 * FatFile.c — 按 8.3 / 路径读根目录文件
 */
#include "FatFile.h"
#include "FatVol.h"
#include "HalBlock.h"

#define MAX_FILE (256u * 1024u)

static int MemEq(const UINT8 *A, const char *B, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        if (A[i] != (UINT8)B[i]) {
            return 0;
        }
    }
    return 1;
}

typedef struct {
    const char *Want83;
    UINT32 Clus;
    UINT32 Size;
    int Found;
} FIND_CTX;

static int OnFind(const UINT8 *Ent, void *Ctx) {
    FIND_CTX *F = (FIND_CTX *)Ctx;
    if (MemEq(Ent, F->Want83, 11)) {
        F->Clus = ((UINT32)FatRd16(Ent + 20) << 16) | FatRd16(Ent + 26);
        F->Size = FatRd32(Ent + 28);
        F->Found = 1;
        return 1;
    }
    return 0;
}

int FatFileRead83(const char Name83[11], void *Buf, UINT32 Cap, UINT32 *OutSize) {
    FAT_VOL V;
    FIND_CTX F;
    UINT8 Sec[FAT_SECTOR];
    UINT8 *Dst = (UINT8 *)Buf;
    UINT32 Got = 0;
    UINT32 Guard;
    UINT32 i;

    if (Buf == 0 || Cap == 0 || Name83 == 0) {
        return -1;
    }
    if (FatVolOpen(&V) != 0) {
        return -1;
    }
    F.Want83 = Name83;
    F.Clus = 0;
    F.Size = 0;
    F.Found = 0;
    if (FatVolWalkRoot(&V, OnFind, &F) != 0 || !F.Found || F.Clus < 2u ||
        F.Size == 0) {
        return -1;
    }
    if (F.Size > Cap || F.Size > MAX_FILE) {
        return -1;
    }
    {
        UINT32 Clus = F.Clus;
        for (Guard = 0; Guard < 512u && Clus >= 2u && Got < F.Size; Guard++) {
            UINT32 Lba = V.DataLba + (Clus - 2u) * (UINT32)V.Spc;
            UINT8 s;
            for (s = 0; s < V.Spc && Got < F.Size; s++) {
                UINT32 Chunk = F.Size - Got;
                if (Chunk > FAT_SECTOR) {
                    Chunk = FAT_SECTOR;
                }
                if (HalBlockRead(Lba + s, Sec, 1) != 0) {
                    return -1;
                }
                for (i = 0; i < Chunk; i++) {
                    Dst[Got + i] = Sec[i];
                }
                Got += Chunk;
            }
            Clus = FatVolNext(&V, Clus);
            if (V.FatBits == 32) {
                if (Clus < 2u || Clus >= 0x0FFFFFF8u) {
                    break;
                }
            } else if (Clus < 2u || Clus >= 0xFFF8u) {
                break;
            }
        }
    }
    if (Got < F.Size) {
        return -1;
    }
    if (OutSize) {
        *OutSize = F.Size;
    }
    return (int)F.Size;
}

int FatFileReadPath(const char *Path, void *Buf, UINT32 Cap, UINT32 *OutSize) {
    char Name83[11];
    if (FatPathTo83(Path, Name83) != 0) {
        return -1;
    }
    return FatFileRead83(Name83, Buf, Cap, OutSize);
}
