/*
 * Gpt.c — K44：找 FAT 分区起点（对标现网 Gpt 薄子集）
 *
 * 【初学者】先认 LBA0 是否 BPB；否则 MBR；type 0xEE 再扫 GPT 项。
 */
#include "Gpt.h"
#include "HalBlock.h"

#define SECTOR 512u

static UINT8 gSec[SECTOR];

static UINT16 Rd16(const UINT8 *P) {
    return (UINT16)(P[0] | ((UINT16)P[1] << 8));
}

static UINT32 Rd32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

static UINT64 Rd64(const UINT8 *P) {
    return (UINT64)Rd32(P) | ((UINT64)Rd32(P + 4) << 32);
}

static int HasBootSig(const UINT8 *S) {
    return S[510] == 0x55u && S[511] == 0xAAu;
}

static int IsFatBpb(const UINT8 *S) {
    UINT16 Bps;
    if (!HasBootSig(S) || (S[0] != 0xEBu && S[0] != 0xE9u)) {
        return 0;
    }
    Bps = Rd16(S + 11);
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

static int Add(GPT_FAT_PART *Out, int Max, int N, UINT32 Lba, int IsEsp) {
    int i;
    if (N >= Max) {
        return N;
    }
    for (i = 0; i < N; i++) {
        if (Out[i].StartLba == Lba) {
            if (IsEsp) {
                Out[i].IsEsp = 1;
            }
            return N;
        }
    }
    Out[N].StartLba = Lba;
    Out[N].IsEsp = IsEsp ? 1 : 0;
    return N + 1;
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

    if (HalBlockRead(1, gSec, 1) != 0) {
        return N;
    }
    if (gSec[0] != 'E' || gSec[1] != 'F' || gSec[2] != 'I' || gSec[3] != ' ' ||
        gSec[4] != 'P' || gSec[5] != 'A' || gSec[6] != 'R' || gSec[7] != 'T') {
        return N;
    }
    EntLba = (UINT32)Rd64(gSec + 72);
    EntN = Rd32(gSec + 80);
    EntSz = Rd32(gSec + 84);
    if (EntSz < 128u || EntN == 0 || EntN > 128u) {
        return N;
    }
    for (i = 0; i < EntN && N < Max; i++) {
        UINT32 Off = (i * EntSz) % SECTOR;
        UINT32 Lba = EntLba + (i * EntSz) / SECTOR;
        UINT64 First;
        int IsEsp = 0;
        int k;

        if (HalBlockRead(Lba, gSec, 1) != 0) {
            break;
        }
        {
            int Empty = 1;
            for (k = 0; k < 16; k++) {
                if (gSec[Off + k] != 0) {
                    Empty = 0;
                    break;
                }
            }
            if (Empty) {
                continue;
            }
        }
        for (k = 0; k < 16; k++) {
            if (gSec[Off + k] != EspGuid[k]) {
                break;
            }
        }
        if (k == 16) {
            IsEsp = 1;
        }
        First = Rd64(gSec + Off + 32);
        if (First == 0 || First > 0xFFFFFFFFu) {
            continue;
        }
        if (HalBlockRead((UINT32)First, gSec, 1) != 0 || !IsFatBpb(gSec)) {
            continue;
        }
        N = Add(Out, Max, N, (UINT32)First, IsEsp);
    }
    return N;
}

int GptFindFatParts(GPT_FAT_PART *Out, int Max, int *OutCount) {
    int N = 0;
    int i;
    int SawGpt = 0;

    if (Out == 0 || Max <= 0 || OutCount == 0 || !HalBlockReady()) {
        return -1;
    }
    *OutCount = 0;
    if (HalBlockRead(0, gSec, 1) != 0) {
        return -1;
    }
    if (IsFatBpb(gSec)) {
        *OutCount = Add(Out, Max, 0, 0, 0);
        return 0;
    }
    if (!HasBootSig(gSec)) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        UINT8 *E = gSec + 446 + i * 16;
        UINT8 Type = E[4];
        UINT32 Lba = Rd32(E + 8);
        if (Type == 0) {
            continue;
        }
        if (Type == 0xEE) {
            SawGpt = 1;
            continue;
        }
        if (Type == 0x0B || Type == 0x0C || Type == 0x0E || Type == 0x06 ||
            Type == 0xEF) {
            if (HalBlockRead(Lba, gSec, 1) == 0 && IsFatBpb(gSec)) {
                N = Add(Out, Max, N, Lba, Type == 0xEF);
            }
            if (HalBlockRead(0, gSec, 1) != 0) {
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
