/*
 * Gpt.c — K44：找 FAT 分区起点（对标现网 Gpt 薄子集）
 *
 * 【初学者】
 * - 分层：Core/FileSystem；VolumeMountAll 消费本 API
 * - 对外：GptFindFatParts
 * - 不做：写分区、非 FAT 文件系统
 */
#include "Gpt.h"
#include "HalBlock.h"

#define SECTOR 512u

static UINT8 gSectorBuffer[SECTOR];

static UINT16 ReadUInt16Le(const UINT8 *Pointer) {
    return (UINT16)(Pointer[0] | ((UINT16)Pointer[1] << 8));
}

static UINT32 ReadUInt32Le(const UINT8 *Pointer) {
    return (UINT32)Pointer[0] | ((UINT32)Pointer[1] << 8) |
           ((UINT32)Pointer[2] << 16) | ((UINT32)Pointer[3] << 24);
}

static UINT64 ReadUInt64Le(const UINT8 *Pointer) {
    return (UINT64)ReadUInt32Le(Pointer) | ((UINT64)ReadUInt32Le(Pointer + 4) << 32);
}

static int HasBootSig(const UINT8 *S) {
    return S[510] == 0x55u && S[511] == 0xAAu;
}

static int IsFatBpb(const UINT8 *S) {
    UINT16 Bps;
    if (!HasBootSig(S) || (S[0] != 0xEBu && S[0] != 0xE9u)) {
        return 0;
    }
    Bps = ReadUInt16Le(S + 11);
    if (Bps != SECTOR || S[13] == 0) {
        return 0;
    }
    if (S[82] == 'F' && S[83] == 'A' && S[84] == 'T') {
        return 1;
    }
    if (S[54] == 'F' && S[55] == 'A' && S[56] == 'T') {
        return 1;
    }
    /* vvfat 等：有合法 BPB 即认 */
    return 1;
}

static int AppendFatPartition(GPT_FAT_PART *Out, int Max, int Count, UINT32 StartLba,
                              int IsEsp) {
    int i;
    if (Count >= Max) {
        return Count;
    }
    for (i = 0; i < Count; i++) {
        if (Out[i].StartLba == StartLba) {
            if (IsEsp) {
                Out[i].IsEsp = 1;
            }
            return Count;
        }
    }
    Out[Count].StartLba = StartLba;
    Out[Count].IsEsp = IsEsp ? 1 : 0;
    return Count + 1;
}

static int ScanGpt(GPT_FAT_PART *Out, int Max, int N) {
    UINT32 EntLba;
    UINT32 EntSz;
    UINT32 EntN;
    UINT32 i;
    /* EFI GUID：C12A7328-F81F-11D2-BA4B-00A0C93EC93B 小端 */
    static const UINT8 EspGuid[16] = {
        0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
        0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

    if (HalBlockRead(1, gSectorBuffer, 1) != 0) {
        return N;
    }
    if (gSectorBuffer[0] != 'E' || gSectorBuffer[1] != 'F' || gSectorBuffer[2] != 'I' || gSectorBuffer[3] != ' ' ||
        gSectorBuffer[4] != 'P' || gSectorBuffer[5] != 'A' || gSectorBuffer[6] != 'R' || gSectorBuffer[7] != 'T') {
        return N;
    }
    EntLba = (UINT32)ReadUInt64Le(gSectorBuffer + 72);
    EntN = ReadUInt32Le(gSectorBuffer + 80);
    EntSz = ReadUInt32Le(gSectorBuffer + 84);
    if (EntSz < 128u || EntN == 0 || EntN > 128u) {
        return N;
    }
    for (i = 0; i < EntN && N < Max; i++) {
        UINT32 Off = (i * EntSz) % SECTOR;
        UINT32 Lba = EntLba + (i * EntSz) / SECTOR;
        UINT64 First;
        int IsEsp = 0;
        int k;

        if (HalBlockRead(Lba, gSectorBuffer, 1) != 0) {
            break;
        }
        {
            int Empty = 1;
            for (k = 0; k < 16; k++) {
                if (gSectorBuffer[Off + k] != 0) {
                    Empty = 0;
                    break;
                }
            }
            if (Empty) {
                continue;
            }
        }
        for (k = 0; k < 16; k++) {
            if (gSectorBuffer[Off + k] != EspGuid[k]) {
                break;
            }
        }
        if (k == 16) {
            IsEsp = 1;
        }
        First = ReadUInt64Le(gSectorBuffer + Off + 32);
        if (First == 0 || First > 0xFFFFFFFFu) {
            continue;
        }
        if (HalBlockRead((UINT32)First, gSectorBuffer, 1) != 0 || !IsFatBpb(gSectorBuffer)) {
            continue;
        }
        N = AppendFatPartition(Out, Max, N, (UINT32)First, IsEsp);
    }
    return N;
}

/*
 * GptFindFatParts — 枚举盘上 FAT 分区起始 LBA
 *
 * 做什么：LBA0 BPB / MBR 四项 / GPT 项；去重；标 ESP。
 * 谁调用：VolumeMountAll。
 * 返回：0 成功（OutCount 可为 0）；-1 参数或读 LBA0 失败
 */
int GptFindFatParts(GPT_FAT_PART *Out, int Max, int *OutCount) {
    int N = 0;
    int i;
    int SawGpt = 0;

    if (Out == 0 || Max <= 0 || OutCount == 0 || !HalBlockReady()) {
        return -1;
    }
    *OutCount = 0;
    if (HalBlockRead(0, gSectorBuffer, 1) != 0) {
        return -1;
    }
    if (IsFatBpb(gSectorBuffer)) {
        *OutCount = AppendFatPartition(Out, Max, 0, 0, 0);
        return 0;
    }
    if (!HasBootSig(gSectorBuffer)) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        UINT8 *E = gSectorBuffer + 446 + i * 16;
        UINT8 Type = E[4];
        UINT32 Lba = ReadUInt32Le(E + 8);
        if (Type == 0) {
            continue;
        }
        if (Type == 0xEE) {
            SawGpt = 1;
            continue;
        }
        if (Type == 0x0B || Type == 0x0C || Type == 0x0E || Type == 0x06 ||
            Type == 0xEF) {
            if (HalBlockRead(Lba, gSectorBuffer, 1) == 0 && IsFatBpb(gSectorBuffer)) {
                N = AppendFatPartition(Out, Max, N, Lba, Type == 0xEF);
            }
            if (HalBlockRead(0, gSectorBuffer, 1) != 0) {
                break;
            }
        }
    }
    if (SawGpt) {
        N = ScanGpt(Out, Max, N);
    }
    *OutCount = N;
    return 0;
}
