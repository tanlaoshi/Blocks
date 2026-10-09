/*
 * FatVol.c — FAT 卷几何与根目录遍历
 */
#include "FatVol.h"
#include "HalBlock.h"

UINT16 FatRd16(const UINT8 *P) {
    return (UINT16)(P[0] | ((UINT16)P[1] << 8));
}

UINT32 FatRd32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

void FatWr16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)(V & 0xFFu);
    P[1] = (UINT8)((V >> 8) & 0xFFu);
}

void FatWr32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)(V & 0xFFu);
    P[1] = (UINT8)((V >> 8) & 0xFFu);
    P[2] = (UINT8)((V >> 16) & 0xFFu);
    P[3] = (UINT8)((V >> 24) & 0xFFu);
}

static int LooksLikeBpb(const UINT8 *Sec) {
    UINT16 Bps = FatRd16(Sec + 11);
    UINT8 Spc = Sec[13];
    if (Sec[510] != 0x55u || Sec[511] != 0xAAu || Bps != FAT_SECTOR || Spc == 0) {
        return 0;
    }
    return (Sec[0] == 0xEBu || Sec[0] == 0xE9u) ? 1 : 0;
}

UINT32 FatVolNext(const FAT_VOL *V, UINT32 Clus) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 EntPerSec;
    UINT32 Lba;
    UINT32 Off;

    if (V == 0) {
        return 0;
    }
    if (V->FatBits == 32) {
        EntPerSec = FAT_SECTOR / 4u;
        Lba = V->FatLba + Clus / EntPerSec;
        Off = (Clus % EntPerSec) * 4u;
        if (HalBlockRead(Lba, Sec, 1) != 0) {
            return 0;
        }
        return FatRd32(Sec + Off) & 0x0FFFFFFFu;
    }
    EntPerSec = FAT_SECTOR / 2u;
    Lba = V->FatLba + Clus / EntPerSec;
    Off = (Clus % EntPerSec) * 2u;
    if (HalBlockRead(Lba, Sec, 1) != 0) {
        return 0;
    }
    return (UINT32)FatRd16(Sec + Off);
}

int FatVolOpen(FAT_VOL *V) {
    UINT8 Sec[FAT_SECTOR];
    UINT16 Reserved;
    UINT8 Nfats;
    UINT16 RootEnt;
    UINT16 FatSz16;
    UINT32 FatSz;
    UINT32 PartLba = 0;
    UINT32 i;

    if (V == 0 || !HalBlockReady()) {
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
                UINT32 Lba = FatRd32(E + 8);
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
    V->Spc = Sec[13];
    Reserved = FatRd16(Sec + 14);
    Nfats = Sec[16];
    RootEnt = FatRd16(Sec + 17);
    FatSz16 = FatRd16(Sec + 22);
    FatSz = FatSz16 ? (UINT32)FatSz16 : FatRd32(Sec + 36);
    V->Nfats = Nfats ? Nfats : 1;
    V->FatSz = FatSz;
    V->FatLba = PartLba + Reserved;
    V->RootSecs = ((UINT32)RootEnt * 32u + (FAT_SECTOR - 1u)) / FAT_SECTOR;
    V->RootLba = V->FatLba + FatSz * (UINT32)V->Nfats;
    V->DataLba = V->RootLba + V->RootSecs;
    V->FatBits = (RootEnt == 0) ? 32u : 16u;
    V->RootClus = (RootEnt == 0) ? FatRd32(Sec + 44) : 0;
    return 0;
}

int FatVolWalkRoot(FAT_VOL *V, FAT_DIR_FN Fn, void *Ctx) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 i;
    UINT32 Guard;

    if (V == 0 || Fn == 0) {
        return -1;
    }
    if (V->FatBits != 32) {
        for (i = 0; i < V->RootSecs && i < 128u; i++) {
            UINTN Off;
            if (HalBlockRead(V->RootLba + i, Sec, 1) != 0) {
                return -1;
            }
            for (Off = 0; Off < FAT_SECTOR; Off += 32u) {
                if (Sec[Off] == 0x00u) {
                    return 0;
                }
                if (Sec[Off] == 0xE5u || (Sec[Off + 11] & 0x08u) != 0 ||
                    (Sec[Off + 11] & 0x0Fu) == 0x0Fu) {
                    continue;
                }
                if (Fn(Sec + Off, Ctx) != 0) {
                    return 0;
                }
            }
        }
        return 0;
    }
    {
        UINT32 Clus = V->RootClus;
        for (Guard = 0; Guard < 64u && Clus >= 2u; Guard++) {
            UINT32 Lba = V->DataLba + (Clus - 2u) * (UINT32)V->Spc;
            UINT8 s;
            for (s = 0; s < V->Spc; s++) {
                UINTN Off;
                if (HalBlockRead(Lba + s, Sec, 1) != 0) {
                    return -1;
                }
                for (Off = 0; Off < FAT_SECTOR; Off += 32u) {
                    if (Sec[Off] == 0x00u) {
                        return 0;
                    }
                    if (Sec[Off] == 0xE5u || (Sec[Off + 11] & 0x08u) != 0 ||
                        (Sec[Off + 11] & 0x0Fu) == 0x0Fu) {
                        continue;
                    }
                    if (Fn(Sec + Off, Ctx) != 0) {
                        return 0;
                    }
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

void FatName83ToDisplay(const UINT8 *N83, char Out[13]) {
    int i;
    int o = 0;
    for (i = 0; i < 8 && N83[i] != ' '; i++) {
        Out[o++] = (char)N83[i];
    }
    if (N83[8] != ' ' || N83[9] != ' ' || N83[10] != ' ') {
        Out[o++] = '.';
        for (i = 8; i < 11 && N83[i] != ' '; i++) {
            Out[o++] = (char)N83[i];
        }
    }
    Out[o] = 0;
}

int FatPathTo83(const char *Path, char Name83[11]) {
    int i;
    int n = 0;
    int e = 0;
    int inExt = 0;

    if (Path == 0) {
        return -1;
    }
    for (i = 0; i < 11; i++) {
        Name83[i] = ' ';
    }
    for (i = 0; Path[i]; i++) {
        char C = Path[i];
        if (C == '/') {
            continue;
        }
        if (C >= 'a' && C <= 'z') {
            C = (char)(C - 'a' + 'A');
        }
        if (C == '.') {
            if (inExt) {
                return -1;
            }
            inExt = 1;
            continue;
        }
        if (!inExt) {
            if (n >= 8) {
                return -1;
            }
            Name83[n++] = C;
        } else {
            if (e >= 3) {
                return -1;
            }
            Name83[8 + e++] = C;
        }
    }
    return n > 0 ? 0 : -1;
}
