/*
 * FatVolume.c — FAT 卷几何与根目录遍历
 *
 * 【初学者】
 * - 分层：Core/FileSystem；块分配见 FatAllocate.c
 * - 对外：FatVolumeOpen / FatVolumeWalkRoot / FatPathTo83 / FatRd16 等
 * - 不做：写目录项、GPT 分区枚举（见 Gpt.c / Volume.c）
 */
#include "FatVolume.h"
#include "HalBlock.h"

/*
 * FatRd16 — 小端读 UINT16
 *
 * 做什么：从磁盘原始字节解包。
 * 谁调用：本目录多数 FAT 模块。
 * 返回：16 位值
 */
UINT16 FatRd16(const UINT8 *P) {
    return (UINT16)(P[0] | ((UINT16)P[1] << 8));
}

/*
 * FatRd32 — 小端读 UINT32
 *
 * 做什么：从磁盘原始字节解包。
 * 谁调用：FatFile / FatDirectory / FatWrite 等。
 * 返回：32 位值
 */
UINT32 FatRd32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

/* FatWr16 / FatWr32 — 小端写 16/32 位；谁调用：FatAllocate / FatWrite / FatMakeDirectory */
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

/*
 * FatVolumeNext — 读 FAT 表中 Cluster 的下一簇
 *
 * 做什么：按 FatBits 16/32 读表项。
 * 谁调用：FatFileRead83 / FatVolumeWalkRoot / FatAllocate。
 * 返回：下一簇；0 表示失败或空闲
 */
UINT32 FatVolumeNext(const FAT_VOLUME *Volume, UINT32 Cluster) {
    UINT8 Sector[FAT_SECTOR];
    UINT32 EntriesPerSector;
    UINT32 LogicalBlock;
    UINT32 Offset;

    if (Volume == 0) {
        return 0;
    }
    if (Volume->FatBits == 32) {
        EntriesPerSector = FAT_SECTOR / 4u;
        LogicalBlock = Volume->FatLba + Cluster / EntriesPerSector;
        Offset = (Cluster % EntriesPerSector) * 4u;
        if (HalBlockRead(LogicalBlock, Sector, 1) != 0) {
            return 0;
        }
        return FatRd32(Sector + Offset) & 0x0FFFFFFFu;
    }
    EntriesPerSector = FAT_SECTOR / 2u;
    LogicalBlock = Volume->FatLba + Cluster / EntriesPerSector;
    Offset = (Cluster % EntriesPerSector) * 2u;
    if (HalBlockRead(LogicalBlock, Sector, 1) != 0) {
        return 0;
    }
    return (UINT32)FatRd16(Sector + Offset);
}

/*
 * FatVolumeOpenAt — 从 PartLba 的 BPB 填充 FAT_VOLUME
 *
 * 做什么：解析 BPB；算 FatLba/RootLba/DataLba；判 FAT16/32。
 * 谁调用：VolumeAddVolume / FatVolumeOpen。
 * 返回：0 成功；非 0 无 BPB 或读失败
 */
int FatVolumeOpenAt(FAT_VOLUME *V, UINT32 PartLba) {
    UINT8 Sec[FAT_SECTOR];
    UINT16 Reserved;
    UINT8 Nfats;
    UINT16 RootEnt;
    UINT16 FatSz16;
    UINT32 FatSz;

    if (V == 0 || !HalBlockReady()) {
        return -1;
    }
    if (HalBlockRead(PartLba, Sec, 1) != 0 || !LooksLikeBpb(Sec)) {
        return -1;
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

/*
 * FatVolumeOpen — 自动选 LBA0 或 MBR 第一 FAT 分区
 *
 * 做什么：读 LBA0；非 BPB 则扫 MBR 分区表。
 * 谁调用：FatActiveVolumeOpen / FatFile / FatProbe 同类路径。
 * 返回：0 成功；非 0 失败
 */
int FatVolumeOpen(FAT_VOLUME *V) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 PartLba = 0;
    UINT32 i;

    if (V == 0 || !HalBlockReady()) {
        return -1;
    }
    if (HalBlockRead(0, Sec, 1) != 0) {
        return -1;
    }
    if (LooksLikeBpb(Sec)) {
        return FatVolumeOpenAt(V, 0);
    }
    if (Sec[510] == 0x55u && Sec[511] == 0xAAu) {
        for (i = 0; i < 4u; i++) {
            UINT8 *E = Sec + 446u + i * 16u;
            UINT8 Type = E[4];
            UINT32 Lba = FatRd32(E + 8);
            if (Type == 0x0Bu || Type == 0x0Cu || Type == 0x06u ||
                Type == 0x0Eu || Type == 0xEFu) {
                PartLba = Lba;
                break;
            }
        }
    }
    if (PartLba == 0) {
        return -1;
    }
    return FatVolumeOpenAt(V, PartLba);
}

/*
 * FatVolumeWalkRoot — 遍历根目录每个有效短名项
 *
 * 做什么：FAT16 扫 RootLba 扇区；FAT32 沿 RootClus 链；回调 Fn。
 * 谁调用：FatDirectoryListRoot / FatFileRead83 / Volume 认 BLOCKS.ID。
 * 返回：0 完成；非 0 IO 失败
 */
int FatVolumeWalkRoot(FAT_VOLUME *V, FAT_DIR_FN Fn, void *Ctx) {
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
        UINT32 Cluster = V->RootClus;
        for (Guard = 0; Guard < 64u && Cluster >= 2u; Guard++) {
            UINT32 Lba = V->DataLba + (Cluster - 2u) * (UINT32)V->Spc;
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
            Cluster = FatVolumeNext(V, Cluster);
            if (Cluster < 2u || Cluster >= 0x0FFFFFF8u) {
                break;
            }
        }
    }
    return 0;
}

/*
 * FatName83ToDisplay — 11 字节 FAT 名 → "NAME.EXT"
 *
 * 做什么：去尾空格、插点；最多 12 字符 + NUL。
 * 谁调用：FatDirectoryListRoot / Volume 标记扫描。
 * 返回：void（写 Out）
 */
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

/*
 * FatPathTo83 — 路径最后分量 → 大写 8.3（11 字节）
 *
 * 做什么：跳过 '/'；名≤8 扩展≤3；非法返回失败。
 * 谁调用：FatPathResolve83 / FatFileReadPath / FatRenamePath。
 * 返回：0 成功；非 0 非法名
 */
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
