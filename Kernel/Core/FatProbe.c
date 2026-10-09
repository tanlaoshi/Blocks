/*
 * FatProbe.c — K16：在块设备上找根目录 TOYOS.ID
 *
 * 【初学者】
 * 读 LBA0（或 MBR 第一分区）BPB → 算根目录 → 扫 8.3 名 "TOYOS   ID"。
 * FAT16 固定根目录；FAT32 跟 FAT 表簇链（有限步）。
 */
#include "BootTypes.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

#define SECTOR 512u

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
    UINT8 Jmp = Sec[0];
    if (Sec[510] != 0x55u || Sec[511] != 0xAAu) {
        return 0;
    }
    if (Bps != SECTOR || Spc == 0) {
        return 0;
    }
    /* EB xx 90 或 E9 */
    if (Jmp == 0xEBu || Jmp == 0xE9u) {
        return 1;
    }
    return 0;
}

/* Dir 扇区里找 TOYOS.ID；找到返回 1 */
static int ScanDirSector(const UINT8 *Sec) {
    UINTN Off;
    for (Off = 0; Off < SECTOR; Off += 32u) {
        if (Sec[Off] == 0x00u) {
            return 0; /* 后续空 */
        }
        if (Sec[Off] == 0xE5u) {
            continue;
        }
        if ((Sec[Off + 11] & 0x08u) != 0) {
            continue; /* volume label */
        }
        if ((Sec[Off + 11] & 0x0Fu) == 0x0Fu) {
            continue; /* LFN */
        }
        if (MemEq(Sec + Off, "TOYOS   ID ", 11)) {
            return 1;
        }
    }
    return 0;
}

/* 读 FAT 表下一簇；FatBits=16/32；失败返回 0 */
static UINT32 FatNextCluster(UINT32 FatLba, UINT32 Clus, UINT32 FatBits) {
    UINT8 Sec[SECTOR];
    UINT32 EntPerSec;
    UINT32 Index;
    UINT32 Lba;
    UINT32 Off;

    if (FatBits == 32) {
        EntPerSec = SECTOR / 4u;
        Index = Clus;
        Lba = FatLba + Index / EntPerSec;
        Off = (Index % EntPerSec) * 4u;
        if (HalBlockRead(Lba, Sec, 1) != 0) {
            return 0;
        }
        return Rd32(Sec + Off) & 0x0FFFFFFFu;
    }
    EntPerSec = SECTOR / 2u;
    Index = Clus;
    Lba = FatLba + Index / EntPerSec;
    Off = (Index % EntPerSec) * 2u;
    if (HalBlockRead(Lba, Sec, 1) != 0) {
        return 0;
    }
    return (UINT32)Rd16(Sec + Off);
}

static int ScanClusterChain(UINT32 DataLba, UINT8 Spc, UINT32 FatLba,
                            UINT32 FatBits, UINT32 StartClus) {
    UINT8 Sec[SECTOR];
    UINT32 Clus = StartClus;
    UINT32 Guard;

    for (Guard = 0; Guard < 64u && Clus >= 2u; Guard++) {
        UINT32 Lba = DataLba + (Clus - 2u) * (UINT32)Spc;
        UINT8 s;
        UINT32 Next;

        for (s = 0; s < Spc; s++) {
            if (HalBlockRead(Lba + s, Sec, 1) != 0) {
                return -1;
            }
            if (ScanDirSector(Sec)) {
                return 0;
            }
        }
        Next = FatNextCluster(FatLba, Clus, FatBits);
        if (FatBits == 32) {
            if (Next < 2u || Next >= 0x0FFFFFF8u) {
                break;
            }
        } else {
            if (Next < 2u || Next >= 0xFFF8u) {
                break;
            }
        }
        Clus = Next;
    }
    return -1;
}

int FatProbeToyOsId(void) {
    UINT8 Sec[SECTOR];
    UINT16 Bps;
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
    UINT32 i;

    if (!HalBlockReady()) {
        return -1;
    }
    if (HalBlockRead(0, Sec, 1) != 0) {
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: fat LBA0 read fail\n");
        return -1;
    }

    if (!LooksLikeBpb(Sec)) {
        /* 可能是 MBR：试第一个 0x0B/0x0C/0x06 分区 */
        if (Sec[510] == 0x55u && Sec[511] == 0xAAu) {
            for (i = 0; i < 4u; i++) {
                UINT8 *E = Sec + 446u + i * 16u;
                UINT8 Type = E[4];
                UINT32 Lba = Rd32(E + 8);
                if (Type == 0x0Bu || Type == 0x0Cu || Type == 0x06u ||
                    Type == 0x0Eu || Type == 0x01u) {
                    PartLba = Lba;
                    break;
                }
            }
        }
        if (PartLba == 0 || HalBlockRead(PartLba, Sec, 1) != 0 ||
            !LooksLikeBpb(Sec)) {
            HalSerialWriteChannel(TOY_SLOG_FS, "Fs: fat no BPB\n");
            return -1;
        }
    }

    Bps = Rd16(Sec + 11);
    Spc = Sec[13];
    Reserved = Rd16(Sec + 14);
    Nfats = Sec[16];
    RootEnt = Rd16(Sec + 17);
    FatSz16 = Rd16(Sec + 22);
    if (Bps != SECTOR || Spc == 0 || Nfats == 0) {
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: fat bad BPB\n");
        return -1;
    }
    FatSz = FatSz16 ? (UINT32)FatSz16 : Rd32(Sec + 36);
    FatLba = PartLba + Reserved;
    RootSecs = ((UINT32)RootEnt * 32u + (Bps - 1u)) / Bps;
    RootLba = FatLba + FatSz * (UINT32)Nfats;
    DataLba = RootLba + RootSecs;

    if (RootEnt != 0) {
        for (i = 0; i < RootSecs && i < 128u; i++) {
            if (HalBlockRead(RootLba + i, Sec, 1) != 0) {
                HalSerialWriteChannel(TOY_SLOG_FS, "Fs: fat root read fail\n");
                return -1;
            }
            if (ScanDirSector(Sec)) {
                return 0;
            }
        }
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: fat root no TOYOS.ID\n");
        return -1;
    }

    /* FAT32 */
    {
        UINT32 RootClus = Rd32(Sec + 44);
        if (RootClus < 2u) {
            HalSerialWriteChannel(TOY_SLOG_FS, "Fs: fat32 bad root clus\n");
            return -1;
        }
        if (ScanClusterChain(DataLba, Spc, FatLba, 32, RootClus) == 0) {
            return 0;
        }
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: fat32 no TOYOS.ID\n");
        return -1;
    }
}
