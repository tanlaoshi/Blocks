/*
 * FatFile.c — K19：根目录按 8.3 名读文件
 *
 * 复用 K16 的 BPB/根目录逻辑；把簇链拷进调用方缓冲。
 */
#include "FatFile.h"
#include "HalBlock.h"

#define SECTOR 512u
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

static UINT16 Rd16(const UINT8 *P) {
    return (UINT16)(P[0] | ((UINT16)P[1] << 8));
}

static UINT32 Rd32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

static int LooksLikeBpb(const UINT8 *Sec) {
    UINT16 Bps = Rd16(Sec + 11);
    UINT8 Spc = Sec[13];
    if (Sec[510] != 0x55u || Sec[511] != 0xAAu || Bps != SECTOR || Spc == 0) {
        return 0;
    }
    return (Sec[0] == 0xEBu || Sec[0] == 0xE9u) ? 1 : 0;
}

static UINT32 FatNext(UINT32 FatLba, UINT32 Clus, UINT32 FatBits) {
    UINT8 Sec[SECTOR];
    UINT32 EntPerSec;
    UINT32 Lba;
    UINT32 Off;

    if (FatBits == 32) {
        EntPerSec = SECTOR / 4u;
        Lba = FatLba + Clus / EntPerSec;
        Off = (Clus % EntPerSec) * 4u;
        if (HalBlockRead(Lba, Sec, 1) != 0) {
            return 0;
        }
        return Rd32(Sec + Off) & 0x0FFFFFFFu;
    }
    EntPerSec = SECTOR / 2u;
    Lba = FatLba + Clus / EntPerSec;
    Off = (Clus % EntPerSec) * 2u;
    if (HalBlockRead(Lba, Sec, 1) != 0) {
        return 0;
    }
    return (UINT32)Rd16(Sec + Off);
}

static int FindInDirSec(const UINT8 *Sec, const char Name83[11], UINT32 *Clus,
                        UINT32 *Size) {
    UINTN Off;
    for (Off = 0; Off < SECTOR; Off += 32u) {
        if (Sec[Off] == 0x00u) {
            return 0;
        }
        if (Sec[Off] == 0xE5u || (Sec[Off + 11] & 0x08u) != 0 ||
            (Sec[Off + 11] & 0x0Fu) == 0x0Fu) {
            continue;
        }
        if (MemEq(Sec + Off, Name83, 11)) {
            *Clus = ((UINT32)Rd16(Sec + Off + 20) << 16) | Rd16(Sec + Off + 26);
            *Size = Rd32(Sec + Off + 28);
            return 1;
        }
    }
    return 0;
}

int FatFileRead83(const char Name83[11], void *Buf, UINT32 Cap, UINT32 *OutSize) {
    UINT8 Sec[SECTOR];
    UINT8 Spc;
    UINT16 Reserved;
    UINT8 Nfats;
    UINT16 RootEnt;
    UINT16 FatSz16;
    UINT32 FatSz;
    UINT32 RootSecs;
    UINT32 FatLba;
    UINT32 RootLba;
    UINT32 DataLba;
    UINT32 PartLba = 0;
    UINT32 FatBits;
    UINT32 FileClus = 0;
    UINT32 FileSize = 0;
    UINT32 i;
    UINT32 Got = 0;
    UINT8 *Dst = (UINT8 *)Buf;
    UINT32 Guard;

    if (!HalBlockReady() || Buf == 0 || Cap == 0 || Name83 == 0) {
        return -1;
    }
    if (HalBlockRead(0, Sec, 1) != 0) {
        return -1;
    }
    if (!LooksLikeBpb(Sec)) {
        if (Sec[510] == 0x55u && Sec[511] == 0xAAu) {
            for (i = 0; i < 4u; i++) {
                UINT8 *E = Sec + 446u + i * 16u;
                UINT8 Type = E[4];
                UINT32 Lba = Rd32(E + 8);
                if (Type == 0x0Bu || Type == 0x0Cu || Type == 0x06u ||
                    Type == 0x0Eu) {
                    PartLba = Lba;
                    break;
                }
            }
        }
        if (PartLba == 0 || HalBlockRead(PartLba, Sec, 1) != 0 ||
            !LooksLikeBpb(Sec)) {
            return -1;
        }
    }

    Spc = Sec[13];
    Reserved = Rd16(Sec + 14);
    Nfats = Sec[16];
    RootEnt = Rd16(Sec + 17);
    FatSz16 = Rd16(Sec + 22);
    FatSz = FatSz16 ? (UINT32)FatSz16 : Rd32(Sec + 36);
    FatLba = PartLba + Reserved;
    RootSecs = ((UINT32)RootEnt * 32u + (SECTOR - 1u)) / SECTOR;
    RootLba = FatLba + FatSz * (UINT32)Nfats;
    DataLba = RootLba + RootSecs;
    FatBits = (RootEnt == 0) ? 32u : 16u;

    if (RootEnt != 0) {
        for (i = 0; i < RootSecs && i < 128u; i++) {
            if (HalBlockRead(RootLba + i, Sec, 1) != 0) {
                return -1;
            }
            if (FindInDirSec(Sec, Name83, &FileClus, &FileSize)) {
                break;
            }
        }
    } else {
        UINT32 RootClus = Rd32(Sec + 44);
        UINT32 Clus = RootClus;
        for (Guard = 0; Guard < 64u && Clus >= 2u && FileClus == 0; Guard++) {
            UINT32 Lba = DataLba + (Clus - 2u) * (UINT32)Spc;
            UINT8 s;
            for (s = 0; s < Spc; s++) {
                if (HalBlockRead(Lba + s, Sec, 1) != 0) {
                    return -1;
                }
                if (FindInDirSec(Sec, Name83, &FileClus, &FileSize)) {
                    break;
                }
            }
            if (FileClus != 0) {
                break;
            }
            Clus = FatNext(FatLba, Clus, FatBits);
            if (FatBits == 32) {
                if (Clus < 2u || Clus >= 0x0FFFFFF8u) {
                    break;
                }
            } else if (Clus < 2u || Clus >= 0xFFF8u) {
                break;
            }
        }
    }

    if (FileClus < 2u || FileSize == 0) {
        return -1;
    }
    if (FileSize > Cap || FileSize > MAX_FILE) {
        return -1;
    }

    {
        UINT32 Clus = FileClus;
        for (Guard = 0; Guard < 512u && Clus >= 2u && Got < FileSize; Guard++) {
            UINT32 Lba = DataLba + (Clus - 2u) * (UINT32)Spc;
            UINT8 s;
            for (s = 0; s < Spc && Got < FileSize; s++) {
                UINT32 Chunk = FileSize - Got;
                if (Chunk > SECTOR) {
                    Chunk = SECTOR;
                }
                if (HalBlockRead(Lba + s, Sec, 1) != 0) {
                    return -1;
                }
                for (i = 0; i < Chunk; i++) {
                    Dst[Got + i] = Sec[i];
                }
                Got += Chunk;
            }
            Clus = FatNext(FatLba, Clus, FatBits);
            if (FatBits == 32) {
                if (Clus < 2u || Clus >= 0x0FFFFFF8u) {
                    break;
                }
            } else if (Clus < 2u || Clus >= 0xFFF8u) {
                break;
            }
        }
    }

    if (Got < FileSize) {
        return -1;
    }
    if (OutSize) {
        *OutSize = FileSize;
    }
    return (int)FileSize;
}
