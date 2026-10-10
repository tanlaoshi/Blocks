/*
 * FatFile.c — 按 8.3 / 路径读根目录文件
 */
#include "FatFile.h"
#include "FatVolume.h"
#include "Volume.h"
#include "HalBlock.h"

/* 允许 CJK32.BIN / CJK.TTF 等盘上字库（~1–2MiB） */
#define MAX_FILE (8u * 1024u * 1024u) /* CJK32.BIN 32×32≈3.7MiB */

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
    FAT_VOLUME V;
    FIND_CTX F;
    UINT8 Sec[FAT_SECTOR];
    UINT8 *Dst = (UINT8 *)Buf;
    UINT32 Got = 0;
    UINT32 Guard;
    UINT32 i;

    if (Buf == 0 || Cap == 0 || Name83 == 0) {
        return -1;
    }
    if (VolumeOpenActive(&V) != 0 && FatVolumeOpen(&V) != 0) {
        return -1;
    }
    F.Want83 = Name83;
    F.Clus = 0;
    F.Size = 0;
    F.Found = 0;
    if (FatVolumeWalkRoot(&V, OnFind, &F) != 0 || !F.Found || F.Clus < 2u ||
        F.Size == 0) {
        return -1;
    }
    if (F.Size > Cap || F.Size > MAX_FILE) {
        return -1;
    }
    {
        UINT32 Clus = F.Clus;
        /* 2MiB / 512B ≈ 4096 扇区；簇链步数放宽 */
        for (Guard = 0; Guard < 16384u && Clus >= 2u && Got < F.Size; Guard++) {
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
            Clus = FatVolumeNext(&V, Clus);
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
    const char *Rel = Path;

    if (Path == 0) {
        return -1;
    }
    (void)VolumeResolve(Path, &Rel);
    if (Rel == 0 || Rel[0] == 0) {
        return -1;
    }
    if (FatPathTo83(Rel, Name83) != 0) {
        return -1;
    }
    return FatFileRead83(Name83, Buf, Cap, OutSize);
}
