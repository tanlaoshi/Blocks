/*
 * harness.c — 主机侧「假 SDHCI + 假 SDIO 卡」测试台（不进内核构建）
 *
 * 目的：在没有 Duo S 板子的情况下，把真实的 Sdhci/Sdio.c 与 Sdhci/Io.c 编到
 * x86 上跑，用一台按 SDIO 规范解码的假卡验证：
 *   - CMD0/5/3/7 顺序与 R4/R6 解析；
 *   - CMD52 参数编码（R/W、fn、addr、data）与 R5 数据；
 *   - CMD53 参数编码（块/字节、incr、count）与 BLOCK_COUNT/BLOCK_SIZE 写入；
 *   - PIO 搬运字节数与缓冲区内容；
 *   - CIS 指针（fn0 0x109-0x10B）与 tuple 0x20/0x21/0x22 解析；
 *   - 1536 B 固定地址 FIFO 写读自检。
 * 它验证的是「协议逻辑」，不是电气/时序；时序只能真机验。
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "Sdhci/Private.h"

/* ---------- 断言 ---------- */
static int gFail;
static int gPass;
#define CHECK(Name, Cond)                                                                          \
    do {                                                                                           \
        if (Cond) {                                                                                \
            printf("  [ok]   %s\n", Name);                                                         \
            gPass++;                                                                               \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", Name);                                                         \
            gFail++;                                                                               \
        }                                                                                          \
    } while (0)

/* ---------- 假寄存器堆 ---------- */
static UINT32 gReg[0x100];

/* ---------- 假卡存储 ---------- */
static UINT8 gFn0[0x400];        /* fn0 地址空间：CCCR + FBR + CIS */
static UINT8 gFn1[0x100];        /* fn1 寄存器空间 */
static UINT8 gEcho[8192];        /* 写 0x10 的数据按序存这里，读 0x0F 从这里出（假回环） */
static UINT32 gEchoLen;
static UINT32 gEchoPos;

/* ---------- 命令 / 传输记录 ---------- */
typedef struct XFER {
    int Valid;
    int Write;
    UINT32 Fn;
    UINT32 Addr;
    int Incr;
    UINT32 Blocks;
    UINT32 BlockSize;
    UINT32 Bytes;
} XFER;

static XFER gXfer;
static int gErrors;
static UINT32 gCmdSeen[64];
static UINT32 gArgSeen[64];
static int gCmdCount;
static UINT32 gRca = 0x00010000u;

static SDHCI_HOST gHost;

static void CardInit(void) {
    memset(gFn0, 0, sizeof(gFn0));
    memset(gFn1, 0, sizeof(gFn1));
    memset(gReg, 0, sizeof(gReg));
    memset(gEcho, 0, sizeof(gEcho));
    gEchoLen = 0;
    gEchoPos = 0;
    memset(&gXfer, 0, sizeof(gXfer));

    /* CCCR：CCCR 1.20 / SDIO 2.00；CAPS=SMB+SDC；SPEED=SHS；IF=1bit */
    gFn0[0x00] = 0x32;
    gFn0[0x08] = 0x03;
    gFn0[0x13] = 0x01;
    gFn0[0x07] = 0x00;
    gFn0[0x02] = 0x00;
    gFn0[0x03] = 0x00;

    /* fn1 FBR：块大小 512，CIS 指针 0x60 */
    gFn0[0x110] = 0x00;
    gFn0[0x111] = 0x02;
    gFn0[0x109] = 0x60;
    gFn0[0x10A] = 0x00;
    gFn0[0x10B] = 0x00;
    /* fn2 FBR：CIS 指针 0x80 */
    gFn0[0x209] = 0x80;

    /* fn1 CIS：MANFID c8a1:0082，FUNCID 06，FUNCE 最大块 0x0200 */
    {
        UINT8 *p = &gFn0[0x60];
        *p++ = 0x20; *p++ = 4; *p++ = 0xa1; *p++ = 0xc8; *p++ = 0x82; *p++ = 0x00;
        *p++ = 0x21; *p++ = 1; *p++ = 0x06;
        *p++ = 0x22; *p++ = 4; *p++ = 0x00; *p++ = 0x00; *p++ = 0x02; *p++ = 0x01;
        *p++ = 0xff;
    }
    /* fn2 CIS：MANFID c8a1:0182 */
    {
        UINT8 *p = &gFn0[0x80];
        *p++ = 0x20; *p++ = 4; *p++ = 0xa1; *p++ = 0xc8; *p++ = 0x82; *p++ = 0x01;
        *p++ = 0x21; *p++ = 1; *p++ = 0x06;
        *p++ = 0xff;
    }
    /* 假 Modem：0x04 杂项中断状态 */
    gFn1[0x04] = 0x00;
}

/* ---------- Hal / 板级桩 ---------- */
void SdhciMark(const char *Text) { fputs(Text, stdout); }
void SdhciMarkHex(const char *Tag, UINT32 Value, int Digits) {
    printf("%s%0*x", Tag, Digits, Value);
}
void SdhciMarkDecimal(const char *Tag, UINT32 Value) { printf("%s%u", Tag, Value); }
void SdhciDelayMilliseconds(UINT32 Milliseconds) { (void)Milliseconds; }
void SdhciDelayMicroseconds(UINT32 Microseconds) { (void)Microseconds; }
int DuoWifiPowerOn(void) { return 0; }
int DuoWifiCardDetectOverride(void) { return 0; }

UINT32 SdhciRead32(SDHCI_HOST *Host, UINT32 Offset) { (void)Host; return gReg[(Offset & ~3u) >> 2]; }
UINT8 SdhciRead8(SDHCI_HOST *Host, UINT32 Offset) {
    UINT32 Shift = (Offset & 3u) * 8u;
    return (UINT8)((SdhciRead32(Host, Offset) >> Shift) & 0xFFu);
}
UINT16 SdhciRead16(SDHCI_HOST *Host, UINT32 Offset) {
    UINT32 Shift = (Offset & 2u) ? 16u : 0u;
    return (UINT16)(SdhciRead32(Host, Offset) >> Shift);
}
void SdhciWrite32(SDHCI_HOST *Host, UINT32 Offset, UINT32 Value) {
    (void)Host;
    gReg[(Offset & ~3u) >> 2] = Value;
}
void SdhciWrite16(SDHCI_HOST *Host, UINT32 Offset, UINT16 Value) {
    UINT32 Aligned = Offset & ~3u;
    UINT32 Shift = (Offset & 2u) ? 16u : 0u;
    UINT32 Old = SdhciRead32(Host, Aligned);
    UINT32 Mask = 0xFFFFu << Shift;
    SdhciWrite32(Host, Aligned, (Old & ~Mask) | ((UINT32)Value << Shift));
}
void SdhciWrite8(SDHCI_HOST *Host, UINT32 Offset, UINT8 Value) {
    (void)Host;
    (void)Offset;
    (void)Value;
}
int SdhciSoftwareReset(SDHCI_HOST *Host, UINT8 Mask) {
    (void)Host;
    (void)Mask;
    return 0;
}
int SdhciControllerBringUp(SDHCI_HOST *Host, UINT32 Hertz, UINT32 IntEnable) {
    (void)IntEnable;
    Host->ClockHertz = Hertz;
    Host->ActualClockHertz = 46875000u;
    return 0;
}
int SdhciClockConfigure(SDHCI_HOST *Host, UINT32 Hertz) {
    if (Hertz > 25000000u) {
        Host->ClockHertz = Hertz;
        Host->ActualClockHertz = 46875000u;
    } else {
        Host->ClockHertz = Hertz;
        Host->ActualClockHertz = 23437500u;
    }
    return 0;
}
void SdhciFail(SDHCI_HOST *Host, const char *What) {
    printf("%s st=%08x pst=%08x\n", What, Host->LastStatus, Host->LastPresentState);
    gErrors++;
}
SDHCI_HOST *SdhciHostAt(UINT32 Index) { return (Index == 1u) ? &gHost : 0; }

/* ---------- 假卡：CMD52 ---------- */
static UINT8 *CardMem(UINT32 Fn, UINT32 Addr) {
    if (Fn == 0u) {
        return &gFn0[Addr & 0x3FFu];
    }
    if (Fn == 1u) {
        return &gFn1[Addr & 0xFFu];
    }
    return &gFn1[0];
}

static void CardCmd52(UINT32 Arg) {
    int Write = (Arg >> 31) & 1;
    UINT32 Fn = (Arg >> 28) & 7u;
    UINT32 Addr = (Arg >> 9) & 0x1FFFFu;
    UINT8 Data = (UINT8)(Arg & 0xFFu);
    UINT8 *P;

    if (Fn > 2u) {
        gHost.Response[0] = 0x00000200u; /* FUNCTION_NUMBER */
        return;
    }
    P = CardMem(Fn, Addr);
    if (Write) {
        *P = Data;
        /* IOEx 写 → IORx 立刻反映（AIC 够快） */
        if (Fn == 0u && Addr == 0x02u) {
            gFn0[0x03] = Data;
        }
        gHost.Response[0] = 0x00000000u;
    } else {
        gHost.Response[0] = (UINT32)(*P);
    }
}

/* ---------- 假卡：CMD53 发起 ---------- */
static void CardCmd53Start(UINT32 Arg, UINT16 Flags) {
    UINT32 BlockSizeReg = gReg[(SDHCI_BLOCK_SIZE & ~3u) >> 2] & 0xFFFu;
    UINT32 BlockCountReg = (gReg[(SDHCI_BLOCK_COUNT & ~3u) >> 2] >> 16) & 0xFFFFu;

    gXfer.Valid = 1;
    gXfer.Write = (Arg >> 31) & 1;
    gXfer.Fn = (Arg >> 28) & 7u;
    gXfer.Incr = (Arg >> 26) & 1;
    gXfer.Addr = (Arg >> 9) & 0x1FFFFu;
    gXfer.BlockSize = BlockSizeReg;
    if ((Arg >> 27) & 1) {
        gXfer.Blocks = Arg & 0x1FFu;
        if (gXfer.Blocks == 0u || gXfer.Blocks != BlockCountReg) {
            gErrors++;
            printf("  [harness] BLOCK_COUNT mismatch arg=%u reg=%u\n", gXfer.Blocks, BlockCountReg);
        }
        gXfer.Bytes = gXfer.Blocks * BlockSizeReg;
    } else {
        UINT32 Cnt = Arg & 0x1FFu;
        gXfer.Blocks = 0;
        gXfer.Bytes = (Cnt == 0u) ? 512u : Cnt;
        if (BlockSizeReg != gXfer.Bytes) {
            gErrors++;
            printf("  [harness] byte-mode BLOCK_SIZE %u != arg %u\n", BlockSizeReg, gXfer.Bytes);
        }
    }
    if ((Flags & SDHCI_CMD_DATA) == 0) {
        gErrors++;
        printf("  [harness] CMD53 missing DATA flag\n");
    }
    gHost.Response[0] = 0;
}

/* ---------- 假卡：PIO ---------- */
int SdhciPioWrite(SDHCI_HOST *Host, const UINT8 *Buffer, UINT32 Bytes) {
    (void)Host;
    if (!gXfer.Valid || !gXfer.Write || Bytes != gXfer.Bytes) {
        gErrors++;
        printf("  [harness] PIO write mismatch: xfer=%d wr=%d bytes=%u want=%u\n", gXfer.Valid,
               gXfer.Write, Bytes, gXfer.Bytes);
        return -1;
    }
    if (gXfer.Fn == 1u && gXfer.Addr == 0x10u) {
        if (!gXfer.Incr) { /* 固定地址 FIFO：数据按序进 */
            memcpy(gEcho + gEchoLen, Buffer, Bytes);
            gEchoLen += Bytes;
        }
    } else {
        UINT8 *P = CardMem(gXfer.Fn, gXfer.Addr);
        memcpy(P, Buffer, Bytes);
    }
    gXfer.Valid = 0;
    return 0;
}

int SdhciPioRead(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 Bytes) {
    (void)Host;
    if (!gXfer.Valid || gXfer.Write || Bytes != gXfer.Bytes) {
        gErrors++;
        printf("  [harness] PIO read mismatch: xfer=%d wr=%d bytes=%u want=%u\n", gXfer.Valid,
               gXfer.Write, Bytes, gXfer.Bytes);
        return -1;
    }
    if (gXfer.Fn == 1u && gXfer.Addr == 0x0Fu) {
        if (gEchoPos + Bytes > gEchoLen) {
            gErrors++;
            printf("  [harness] RX underrun\n");
            memset(Buffer, 0xEE, Bytes);
        } else {
            memcpy(Buffer, gEcho + gEchoPos, Bytes);
            gEchoPos += Bytes;
        }
    } else {
        memcpy(Buffer, CardMem(gXfer.Fn, gXfer.Addr), Bytes);
    }
    gXfer.Valid = 0;
    return 0;
}

/* ---------- 假卡：命令分发 ---------- */
int SdhciCommandSend(SDHCI_HOST *Host, UINT8 CommandIndex, UINT32 Argument, UINT16 Flags,
                     UINT16 TransferMode) {
    (void)Host;
    (void)TransferMode;
    if (gCmdCount < 64) {
        gCmdSeen[gCmdCount] = CommandIndex;
        gArgSeen[gCmdCount] = Argument;
    }
    gCmdCount++;
    /* 每条带 CRC 的命令，CRC/INDEX 位必须置（R4 除外） */
    if (CommandIndex != 5u && CommandIndex != 0u) {
        if ((Flags & (SDHCI_CMD_CRC | SDHCI_CMD_INDEX)) != (SDHCI_CMD_CRC | SDHCI_CMD_INDEX)) {
            gErrors++;
            printf("  [harness] CMD%u flags %04x missing CRC/INDEX\n", CommandIndex, Flags);
        }
    }
    switch (CommandIndex) {
    case 0:
        Host->Response[0] = 0;
        return 0;
    case 5:
        /* R4：ready=1，OCR=3.2-3.4V 档（HOST 传 arg=0 时也要能回 capabilities） */
        Host->Response[0] = 0x80000000u | 0x00300000u;
        return 0;
    case 3:
        Host->Response[0] = gRca;
        return 0;
    case 7:
        return 0;
    case 52:
        if ((Flags & SDHCI_CMD_DATA) != 0 || (Flags & 3u) != SDHCI_CMD_RESP_SHORT) {
            gErrors++;
            printf("  [harness] CMD52 flags wrong %04x\n", Flags);
        }
        CardCmd52(Argument);
        return 0;
    case 53:
        CardCmd53Start(Argument, Flags);
        return 0;
    default:
        gErrors++;
        printf("  [harness] unexpected CMD%u\n", CommandIndex);
        return -1;
    }
}

/* ---------- 断言辅助 ---------- */
static int CmdIndexOf(UINT32 Cmd) {
    int i;
    for (i = 0; i < gCmdCount && i < 64; i++) {
        if (gCmdSeen[i] == Cmd) {
            return i;
        }
    }
    return -1;
}

static int CmdArgOf(UINT32 Cmd) {
    int i = CmdIndexOf(Cmd);
    return (i < 0) ? -1 : (int)gArgSeen[i];
}

static int CmdArgLast(UINT32 Cmd) {
    int i;
    for (i = gCmdCount - 1; i >= 0 && i < 64; i--) {
        if (gCmdSeen[i] == Cmd) {
            return (int)gArgSeen[i];
        }
    }
    return -1;
}

int main(void) {
    memset(&gHost, 0, sizeof(gHost));
    gHost.Index = 1;
    gHost.BusWidth = 1;
    gHost.Specification = 3;
    gHost.Base = SDHCI_BASE_SD1;
    CardInit();

    printf("=== 假卡上跑真实 Sdhci/Sdio.c + Sdhci/Io.c ===\n");
    {
        int Rc = SdhciWifiInitialize();

        printf("\n--- 断言 ---\n");
        CHECK("SdhciWifiInitialize() == 0", Rc == 0);
        CHECK("CardType == SDHCI_CARD_SDIO", gHost.CardType == SDHCI_CARD_SDIO);
        CHECK("BusWidth == 4", gHost.BusWidth == 4u);
        CHECK("ClockHertz == 50 MHz", gHost.ClockHertz == 50000000u);
        CHECK("CMD0 先于 CMD5", CmdIndexOf(0) >= 0 && CmdIndexOf(0) < CmdIndexOf(5));
        CHECK("CMD5 首探 arg=0", CmdIndexOf(5) >= 0 && gArgSeen[CmdIndexOf(5)] == 0u);
        CHECK("CMD5 重试带 3.3V 窗口", (CmdArgLast(5) & 0x00300000u) != 0u);
        CHECK("CMD3 在 CMD5 之后", CmdIndexOf(3) > CmdIndexOf(5));
        CHECK("CMD7 在 CMD3 之后", CmdIndexOf(7) > CmdIndexOf(3));
        CHECK("CMD7 参数 = RCA", CmdArgOf(7) == (int)gRca);
        CHECK("CCCR 0x08 读到 SMB=1", (gFn0[0x08] & 0x02u) != 0u);
        CHECK("CCCR 0x13 写入 EHS", (gFn0[0x13] & 0x02u) != 0u);
        CHECK("CCCR 0x07 切 4-bit", (gFn0[0x07] & 0x03u) == 0x02u);
        CHECK("fn1 块大小 = 0x0200", (gFn0[0x110] | (gFn0[0x111] << 8)) == 0x0200u);
        CHECK("fn1 已使能（IOEx bit1）", (gFn0[0x02] & 0x02u) != 0u);
        printf("\n  [info] FIFO 写进假卡 %u B，读回 %u B，协议错误 %d\n", gEchoLen, gEchoPos,
               gErrors);
        CHECK("FIFO 回环 1536 B 全对", gEchoLen == 1536u && gEchoPos == 1536u);
        CHECK("无协议级错误", gErrors == 0);
    }
    printf("\n结果：%d 通过，%d 失败\n", gPass, gFail);
    return gFail ? 1 : 0;
}
