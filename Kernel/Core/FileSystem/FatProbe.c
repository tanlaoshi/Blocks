/*
 * FatProbe.c — 在块设备上找根目录 BLOCKS.ID
 *
 * 【初学者】
 * - 分层：Core/FileSystem；Boot 侧也可用同类逻辑
 * - 对外：FatProbeOsMarker
 * - 不做：挂卷、写盘；FAT16 固定根 / FAT32 有限步簇链
 */
#include "BootTypes.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define SECTOR 512u

static int MemoryEqual(const UINT8 *A, const char *B, UINTN N) {
    UINTN Index;
    for (Index = 0; Index < N; Index++) {
        if (A[Index] != (UINT8)B[Index]) {
            return 0;
        }
    }
    return 1;
}

static UINT16 ReadUInt16Le(const UINT8 *Pointer) {
    return (UINT16)(Pointer[0] | ((UINT16)Pointer[1] << 8));
}

static UINT32 ReadUInt32Le(const UINT8 *Pointer) {
    return (UINT32)Pointer[0] | ((UINT32)Pointer[1] << 8) |
           ((UINT32)Pointer[2] << 16) | ((UINT32)Pointer[3] << 24);
}

static int LooksLikeBpb(const UINT8 *Sector) {
    UINT16 BytesPerSector = ReadUInt16Le(Sector + 11);
    UINT8 SectorsPerCluster = Sector[13];
    UINT8 Jump = Sector[0];
    if (Sector[510] != 0x55u || Sector[511] != 0xAAu) {
        return 0;
    }
    if (BytesPerSector != SECTOR || SectorsPerCluster == 0) {
        return 0;
    }
    if (Jump == 0xEBu || Jump == 0xE9u) {
        return 1;
    }
    return 0;
}

static int ScanDirSector(const UINT8 *Sector) {
    UINTN Offset;
    for (Offset = 0; Offset < SECTOR; Offset += 32u) {
        if (Sector[Offset] == 0x00u) {
            return 0;
        }
        if (Sector[Offset] == 0xE5u) {
            continue;
        }
        if ((Sector[Offset + 11] & 0x08u) != 0) {
            continue;
        }
        if ((Sector[Offset + 11] & 0x0Fu) == 0x0Fu) {
            continue;
        }
        if (MemoryEqual(Sector + Offset, "BLOCKS  ID ", 11) ||
            MemoryEqual(Sector + Offset, "TOYOS   ID ", 11)) {
            return 1;
        }
    }
    return 0;
}

static UINT32 FatNextCluster(UINT32 FatLba, UINT32 Cluster, UINT32 FatBits) {
    UINT8 Sector[SECTOR];
    UINT32 EntriesPerSector;
    UINT32 Index;
    UINT32 LogicalBlock;
    UINT32 Offset;

    if (FatBits == 32) {
        EntriesPerSector = SECTOR / 4u;
        Index = Cluster;
        LogicalBlock = FatLba + Index / EntriesPerSector;
        Offset = (Index % EntriesPerSector) * 4u;
        if (HalBlockRead(LogicalBlock, Sector, 1) != 0) {
            return 0;
        }
        return ReadUInt32Le(Sector + Offset) & 0x0FFFFFFFu;
    }
    EntriesPerSector = SECTOR / 2u;
    Index = Cluster;
    LogicalBlock = FatLba + Index / EntriesPerSector;
    Offset = (Index % EntriesPerSector) * 2u;
    if (HalBlockRead(LogicalBlock, Sector, 1) != 0) {
        return 0;
    }
    return (UINT32)ReadUInt16Le(Sector + Offset);
}

static int ScanClusterChain(UINT32 DataLba, UINT8 SectorsPerCluster, UINT32 FatLba,
                            UINT32 FatBits, UINT32 StartCluster) {
    UINT8 Sector[SECTOR];
    UINT32 Cluster = StartCluster;
    UINT32 Guard;

    for (Guard = 0; Guard < 64u && Cluster >= 2u; Guard++) {
        UINT32 LogicalBlock = DataLba + (Cluster - 2u) * (UINT32)SectorsPerCluster;
        UINT8 SectorInCluster;
        UINT32 Next;

        for (SectorInCluster = 0; SectorInCluster < SectorsPerCluster; SectorInCluster++) {
            if (HalBlockRead(LogicalBlock + SectorInCluster, Sector, 1) != 0) {
                return -1;
            }
            if (ScanDirSector(Sector)) {
                return 0;
            }
        }
        Next = FatNextCluster(FatLba, Cluster, FatBits);
        if (FatBits == 32) {
            if (Next < 2u || Next >= 0x0FFFFFF8u) {
                break;
            }
        } else {
            if (Next < 2u || Next >= 0xFFF8u) {
                break;
            }
        }
        Cluster = Next;
    }
    return -1;
}

/*
 * FatProbeOsMarker — 块设备上是否存在 BLOCKS.ID / TOYOS.ID
 *
 * 做什么：解析 BPB/MBR；扫根目录或 FAT32 根簇链。
 * 谁调用：Boot 装卷前探测（若链接）；诊断路径。
 * 返回：0 找到；非 0 未找到或 IO 失败
 */
int FatProbeOsMarker(void) {
    UINT8 Sector[SECTOR];
    UINT16 BytesPerSector;
    UINT8 SectorsPerCluster;
    UINT16 Reserved;
    UINT8 NumberOfFats;
    UINT16 RootEntryCount;
    UINT16 FatSize16;
    UINT32 FatSize;
    UINT32 RootSectorCount;
    UINT32 FatLba;
    UINT32 RootLba;
    UINT32 DataLba;
    UINT32 PartitionLba = 0;
    UINT32 Index;

    if (!HalBlockReady()) {
        return -1;
    }
    if (HalBlockRead(0, Sector, 1) != 0) {
        HalSerialWriteChannel(SLOG_FS, "Fs: fat LBA0 read fail\n");
        return -1;
    }

    if (!LooksLikeBpb(Sector)) {
        if (Sector[510] == 0x55u && Sector[511] == 0xAAu) {
            for (Index = 0; Index < 4u; Index++) {
                UINT8 *Entry = Sector + 446u + Index * 16u;
                UINT8 Type = Entry[4];
                UINT32 Lba = ReadUInt32Le(Entry + 8);
                if (Type == 0x0Bu || Type == 0x0Cu || Type == 0x06u ||
                    Type == 0x0Eu || Type == 0x01u) {
                    PartitionLba = Lba;
                    break;
                }
            }
        }
        if (PartitionLba == 0 || HalBlockRead(PartitionLba, Sector, 1) != 0 ||
            !LooksLikeBpb(Sector)) {
            HalSerialWriteChannel(SLOG_FS, "Fs: fat no BPB\n");
            return -1;
        }
    }

    BytesPerSector = ReadUInt16Le(Sector + 11);
    SectorsPerCluster = Sector[13];
    Reserved = ReadUInt16Le(Sector + 14);
    NumberOfFats = Sector[16];
    RootEntryCount = ReadUInt16Le(Sector + 17);
    FatSize16 = ReadUInt16Le(Sector + 22);
    if (BytesPerSector != SECTOR || SectorsPerCluster == 0 || NumberOfFats == 0) {
        HalSerialWriteChannel(SLOG_FS, "Fs: fat bad BPB\n");
        return -1;
    }
    FatSize = FatSize16 ? (UINT32)FatSize16 : ReadUInt32Le(Sector + 36);
    FatLba = PartitionLba + Reserved;
    RootSectorCount = ((UINT32)RootEntryCount * 32u + (BytesPerSector - 1u)) / BytesPerSector;
    RootLba = FatLba + FatSize * (UINT32)NumberOfFats;
    DataLba = RootLba + RootSectorCount;

    if (RootEntryCount != 0) {
        for (Index = 0; Index < RootSectorCount && Index < 128u; Index++) {
            if (HalBlockRead(RootLba + Index, Sector, 1) != 0) {
                HalSerialWriteChannel(SLOG_FS, "Fs: fat root read fail\n");
                return -1;
            }
            if (ScanDirSector(Sector)) {
                return 0;
            }
        }
        HalSerialWriteChannel(SLOG_FS, "Fs: fat root no BLOCKS.ID\n");
        return -1;
    }

    {
        UINT32 RootCluster = ReadUInt32Le(Sector + 44);
        if (RootCluster < 2u) {
            HalSerialWriteChannel(SLOG_FS, "Fs: fat32 bad root clus\n");
            return -1;
        }
        if (ScanClusterChain(DataLba, SectorsPerCluster, FatLba, 32, RootCluster) == 0) {
            return 0;
        }
        HalSerialWriteChannel(SLOG_FS, "Fs: fat32 no BLOCKS.ID\n");
        return -1;
    }
}
