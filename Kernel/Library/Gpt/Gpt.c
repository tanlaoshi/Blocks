/*
 * Gpt.c — 查找 FAT 分区（PR-S-gpt-1）
 * 写盘布局：GptWrite.c
 */
#include "GptPrivate.h"
#include "Block.h"
#include "LibWrite.h"

UINT8 gGptSector[SECTOR];

/* 检查扇区末尾 0x55AA 引导签名 */
static int SectorHasBootSig(void) {
    return gGptSector[510] == 0x55 && gGptSector[511] == 0xAA;
}

/* 判断缓冲区是否为 FAT 引导扇区（superfloppy / 分区起点） */
static int IsFatBootSector(void) {
    if (!SectorHasBootSig()) {
        return 0;
    }
    if (gGptSector[0] != 0xEB && gGptSector[0] != 0xE9) {
        return 0;
    }
    if (gGptSector[82] == 'F' && gGptSector[83] == 'A' && gGptSector[84] == 'T') {
        return 1;
    }
    if (gGptSector[54] == 'F' && gGptSector[55] == 'A' && gGptSector[56] == 'T') {
        return 1;
    }
    return 0;
}

static int LbaLooksLikeFat(UINT32 Lba) {
    if (!BlockReadSectors(Lba, 1, gGptSector)) {
        return 0;
    }
    return IsFatBootSector();
}

static int AddPart(GPT_FAT_PART *Out, int Max, int Count, UINT32 Lba, int IsEsp,
                   const UINT8 *PartGuid) {
    int i;
    int g;

    if (Count >= Max || !Out) {
        return Count;
    }
    for (i = 0; i < Count; i++) {
        if (Out[i].StartLba == Lba) {
            if (IsEsp) {
                Out[i].IsEsp = 1;
            }
            if (PartGuid && !Out[i].PartGuidOk) {
                for (g = 0; g < 16; g++) {
                    Out[i].PartGuid[g] = PartGuid[g];
                }
                Out[i].PartGuidOk = 1;
            }
            return Count;
        }
    }
    Out[Count].StartLba = Lba;
    Out[Count].IsEsp = IsEsp ? 1 : 0;
    Out[Count].PartGuidOk = 0;
    if (PartGuid) {
        for (g = 0; g < 16; g++) {
            Out[Count].PartGuid[g] = PartGuid[g];
        }
        Out[Count].PartGuidOk = 1;
    }
    return Count + 1;
}

/* 解析 MBR 分区表：收集全部 FAT 类型分区；返回 2=GPT 保护分区 */
static int MbrCollectFat(GPT_FAT_PART *Out, int Max, int *InOutCount) {
    int SawGpt = 0;
    int i;

    if (!SectorHasBootSig()) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        UINT8 *P = gGptSector + 0x1BE + i * 16;
        UINT8 Type = P[4];
        UINT32 Start;

        if (Type == 0x00) {
            continue;
        }
        if (Type == 0xEE) {
            SawGpt = 1;
            continue;
        }
        /*
         * 常见 FAT 类型直接收；其它（含 0x83：Pi 卡曾是 ext4，后 mkfs.vfat
         * 只改内容不改类型）用首扇区探测，否则 BLOCKS 永挂不上 → default=ESP。
         */
        Start = (UINT32)P[8] | ((UINT32)P[9] << 8) | ((UINT32)P[10] << 16) |
                ((UINT32)P[11] << 24);
        if (Type == 0x0B || Type == 0x0C || Type == 0x0E || Type == 0x06 ||
            Type == 0x04 || Type == 0xEF) {
            int IsEsp = (Type == 0xEF) ? 1 : 0;
            *InOutCount = AddPart(Out, Max, *InOutCount, Start, IsEsp, 0);
        } else if (Start != 0) {
            int Fat = LbaLooksLikeFat(Start);
            /* LbaLooksLikeFat 覆写扇区缓冲；必须读回 MBR 再扫下一槽 */
            if (!BlockReadSectors(0, 1, gGptSector)) {
                return SawGpt ? 2 : (*InOutCount > 0 ? 1 : 0);
            }
            if (Fat) {
                *InOutCount = AddPart(Out, Max, *InOutCount, Start, 0, 0);
                LibWrite("FS: FAT LBA ");
                LibWriteHex32(Start);
                LibWrite(" (MBR type=");
                LibWriteHex32(Type);
                LibWrite(" probed)\n");
            }
        }
    }
    return SawGpt ? 2 : (*InOutCount > 0 ? 1 : 0);
}

static int GuidEq(const UINT8 *A, const UINT8 *B) {
    int i;

    for (i = 0; i < 16; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

/*
 * 枚举 GPT：EFI System + Microsoft Basic Data；
 * 其它 GUID 若首扇区像 FAT 也收录（避免类型标错漏挂 BLOCKS）。
 */
static int GptCollectFat(GPT_FAT_PART *Out, int Max, int *InOutCount) {
    if (!BlockReadSectors(1, 1, gGptSector)) {
        return 0;
    }
    if (*(UINT64 *)(void *)gGptSector != 0x5452415020494645ULL) {
        return 0;
    }
    {
        UINT32 EntriesLba = *(UINT32 *)(void *)(gGptSector + 72);
        UINT32 EntryCount = *(UINT32 *)(void *)(gGptSector + 80);
        UINT32 EntrySize = *(UINT32 *)(void *)(gGptSector + 84);
        UINT8 Entry[128];
        UINT32 i;

        if (EntrySize < 128 || EntryCount == 0 || EntryCount > 128) {
            return 0;
        }
        for (i = 0; i < EntryCount; i++) {
            UINT32 Sec = EntriesLba + (i * EntrySize) / SECTOR;
            UINT32 Off = (i * EntrySize) % SECTOR;
            UINT32 j;
            UINT64 First;
            UINT64 Last;
            int IsEsp;
            int Want;

            if (Off + 128 > SECTOR) {
                continue; /* 跨扇区项少见；跳过避免读穿 */
            }
            if (!BlockReadSectors(Sec, 1, gGptSector)) {
                return 0;
            }
            for (j = 0; j < 128; j++) {
                Entry[j] = gGptSector[Off + j];
            }
            First = *(UINT64 *)(void *)(Entry + 32);
            Last = *(UINT64 *)(void *)(Entry + 40);
            if (First == 0 && Last == 0) {
                continue;
            }
            IsEsp = GuidEq(Entry, EFI_PART);
            Want = IsEsp || GuidEq(Entry, BASIC_DATA);
            if (!Want) {
                /* GUID 未识别：仍探测是否 FAT（真机偶发类型非 0700） */
                if (!LbaLooksLikeFat((UINT32)First)) {
                    continue;
                }
            } else if (!IsEsp) {
                /* Basic Data 也可能是 NTFS；非 FAT 则跳过 */
                if (!LbaLooksLikeFat((UINT32)First)) {
                    continue;
                }
            } else {
                /* ESP 几乎总是 FAT；失败则跳过 */
                if (!LbaLooksLikeFat((UINT32)First)) {
                    continue;
                }
            }
            /* Unique partition GUID at entry+16 */
            *InOutCount = AddPart(Out, Max, *InOutCount, (UINT32)First, IsEsp,
                                  Entry + 16);
        }
    }
    return *InOutCount > 0 ? 1 : 0;
}

int GptCurrentDiskGuid(UINT8 Out[16]) {
    int i;

    if (!Out) {
        return 0;
    }
    if (!BlockReadSectors(1, 1, gGptSector)) {
        return 0;
    }
    if (gGptSector[0] != 'E' || gGptSector[1] != 'F' || gGptSector[2] != 'I' ||
        gGptSector[3] != ' ' || gGptSector[4] != 'P' || gGptSector[5] != 'A' ||
        gGptSector[6] != 'R' || gGptSector[7] != 'T') {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        Out[i] = gGptSector[56 + i];
    }
    return 1;
}

void GptFormatGuidUuid(const UINT8 Bin[16], char Out[37]) {
    static const char Hex[] = "0123456789abcdef";
    UINT32 D1;
    UINT16 D2;
    UINT16 D3;
    int i = 0;

    if (!Bin || !Out) {
        return;
    }
    D1 = (UINT32)Bin[0] | ((UINT32)Bin[1] << 8) | ((UINT32)Bin[2] << 16) |
         ((UINT32)Bin[3] << 24);
    D2 = (UINT16)((UINT16)Bin[4] | ((UINT16)Bin[5] << 8));
    D3 = (UINT16)((UINT16)Bin[6] | ((UINT16)Bin[7] << 8));
    Out[i++] = Hex[(D1 >> 28) & 0xF];
    Out[i++] = Hex[(D1 >> 24) & 0xF];
    Out[i++] = Hex[(D1 >> 20) & 0xF];
    Out[i++] = Hex[(D1 >> 16) & 0xF];
    Out[i++] = Hex[(D1 >> 12) & 0xF];
    Out[i++] = Hex[(D1 >> 8) & 0xF];
    Out[i++] = Hex[(D1 >> 4) & 0xF];
    Out[i++] = Hex[D1 & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(D2 >> 12) & 0xF];
    Out[i++] = Hex[(D2 >> 8) & 0xF];
    Out[i++] = Hex[(D2 >> 4) & 0xF];
    Out[i++] = Hex[D2 & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(D3 >> 12) & 0xF];
    Out[i++] = Hex[(D3 >> 8) & 0xF];
    Out[i++] = Hex[(D3 >> 4) & 0xF];
    Out[i++] = Hex[D3 & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(Bin[8] >> 4) & 0xF];
    Out[i++] = Hex[Bin[8] & 0xF];
    Out[i++] = Hex[(Bin[9] >> 4) & 0xF];
    Out[i++] = Hex[Bin[9] & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(Bin[10] >> 4) & 0xF];
    Out[i++] = Hex[Bin[10] & 0xF];
    Out[i++] = Hex[(Bin[11] >> 4) & 0xF];
    Out[i++] = Hex[Bin[11] & 0xF];
    Out[i++] = Hex[(Bin[12] >> 4) & 0xF];
    Out[i++] = Hex[Bin[12] & 0xF];
    Out[i++] = Hex[(Bin[13] >> 4) & 0xF];
    Out[i++] = Hex[Bin[13] & 0xF];
    Out[i++] = Hex[(Bin[14] >> 4) & 0xF];
    Out[i++] = Hex[Bin[14] & 0xF];
    Out[i++] = Hex[(Bin[15] >> 4) & 0xF];
    Out[i++] = Hex[Bin[15] & 0xF];
    Out[i] = 0;
}

static int HexNibble(char C) {
    if (C >= '0' && C <= '9') {
        return C - '0';
    }
    if (C >= 'a' && C <= 'f') {
        return C - 'a' + 10;
    }
    if (C >= 'A' && C <= 'F') {
        return C - 'A' + 10;
    }
    return -1;
}

int GptParseUuidStr(const char *Str, UINT8 Out[16]) {
    /* 8-4-4-4-12 with mixed-endian first three fields */
    static const int Pos[16] = {
        0, 2, 4, 6, 9, 11, 14, 16, 19, 21, 24, 26, 28, 30, 32, 34
    };
    static const int OutI[16] = {
        3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15
    };
    int i;

    if (!Str || !Out) {
        return 0;
    }
    for (i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (Str[i] != '-') {
                return 0;
            }
        } else if (HexNibble(Str[i]) < 0) {
            return 0;
        }
    }
    if (Str[36] != 0 && Str[36] != '\n' && Str[36] != '\r' && Str[36] != ' ') {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        int Hi = HexNibble(Str[Pos[i]]);
        int Lo = HexNibble(Str[Pos[i] + 1]);

        if (Hi < 0 || Lo < 0) {
            return 0;
        }
        Out[OutI[i]] = (UINT8)((Hi << 4) | Lo);
    }
    return 1;
}

int GptFindAllFat(GPT_FAT_PART *Out, int Max) {
    int Count = 0;
    int Mbr;

    if (!Out || Max <= 0) {
        return 0;
    }
    if (!BlockReadSectors(0, 1, gGptSector)) {
        return 0;
    }
    if (IsFatBootSector()) {
        return AddPart(Out, Max, 0, 0, 0, 0);
    }
    Mbr = MbrCollectFat(Out, Max, &Count);
    if (Mbr == 2) {
        Count = 0;
        if (!GptCollectFat(Out, Max, &Count)) {
            LibWrite("FS: GPT but no FAT partitions\n");
            return 0;
        }
    } else if (Count <= 0) {
        LibWrite("FS: no FAT partition found\n");
        return 0;
    }
    {
        int i;
        for (i = 0; i < Count; i++) {
            LibWrite("FS: FAT LBA ");
            LibWriteHex32(Out[i].StartLba);
            if (Out[i].IsEsp) {
                LibWrite(" (ESP)");
            }
            LibWrite("\n");
        }
    }
    return Count;
}

int GptFindFatStartEx(UINT32 *OutLba, int *OutIsEsp) {
    GPT_FAT_PART Parts[GPT_MAX_FAT_PARTS];
    int N;
    int i;
    int Prefer = -1;

    if (OutIsEsp) {
        *OutIsEsp = 0;
    }
    if (!OutLba) {
        return 0;
    }
    N = GptFindAllFat(Parts, GPT_MAX_FAT_PARTS);
    if (N <= 0) {
        return 0;
    }
    /* 兼容旧语义：优先非 ESP（BLOCKS/数据卷），否则 ESP */
    for (i = 0; i < N; i++) {
        if (!Parts[i].IsEsp) {
            Prefer = i;
            break;
        }
    }
    if (Prefer < 0) {
        Prefer = 0;
    }
    *OutLba = Parts[Prefer].StartLba;
    if (OutIsEsp) {
        *OutIsEsp = Parts[Prefer].IsEsp;
    }
    return 1;
}

int GptFindFatStart(UINT32 *OutLba) {
    return GptFindFatStartEx(OutLba, 0);
}
