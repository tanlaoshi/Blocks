/*
 * HalXhci.c — X64：xHCI 最小复位 + 端口 CCS（K14）
 *
 * 【初学者】
 * 能力区：CAPLENGTH / HCSPARAMS1。
 * 操作区：USBCMD（RS/HCRST）、USBSTS（HCH/CNR）、PORTSC（CCS/PP）。
 * 不建命令环 / 事件环，不 EnableSlot。
 */
#include "HalXhci.h"

#define CAP_CAPLENGTH   0x00u
#define CAP_HCSPARAMS1  0x04u
#define OP_USBCMD       0x00u
#define OP_USBSTS       0x04u
#define OP_PORTSC       0x400u

#define USBCMD_RS       (1u << 0)
#define USBCMD_HCRST    (1u << 1)
#define USBSTS_HCH      (1u << 0)
#define USBSTS_CNR      (1u << 11)
#define PORTSC_CCS      (1u << 0)
#define PORTSC_PP       (1u << 9)
/* PORTSC 写 1 清除的变更位：写回时必须置 0，以免误清 */
#define PORTSC_CHANGE \
    ((1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | \
     (1u << 22) | (1u << 23))

#define WAIT_SPIN_MAX   2000000u

static UINT64 gVirt;
static UINT8 gCapLen;
static UINT32 gMaxPorts;
static int gReady;

static volatile UINT32 *Cap32(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(gVirt + Off);
}

static volatile UINT32 *Op32(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(gVirt + (UINT64)gCapLen + Off);
}

static void Pause(void) {
    __asm__ volatile("pause");
}

static int WaitBitsClear(volatile UINT32 *Reg, UINT32 Mask) {
    UINT32 i;

    for (i = 0; i < WAIT_SPIN_MAX; i++) {
        if (((*Reg) & Mask) == 0) {
            return 0;
        }
        Pause();
    }
    return -1;
}

static int WaitBitsSet(volatile UINT32 *Reg, UINT32 Mask) {
    UINT32 i;

    for (i = 0; i < WAIT_SPIN_MAX; i++) {
        if (((*Reg) & Mask) == Mask) {
            return 0;
        }
        Pause();
    }
    return -1;
}

int HalXhciAttach(UINT64 Virt) {
    UINT8 Cap;
    UINT32 Params;

    gVirt = 0;
    gCapLen = 0;
    gMaxPorts = 0;
    gReady = 0;

    if (Virt == 0 || (Virt & 0xFu) != 0) {
        return -1;
    }
    Cap = *(volatile UINT8 *)(UINTN)Virt;
    if (Cap < 0x20u || Cap > 0x80u) {
        return -1;
    }
    gVirt = Virt;
    gCapLen = Cap;
    Params = *Cap32(CAP_HCSPARAMS1);
    gMaxPorts = (Params >> 24) & 0xFFu;
    if (gMaxPorts == 0) {
        gMaxPorts = 1;
    }
    if (gMaxPorts > 255u) {
        gMaxPorts = 255u;
    }
    gReady = 1;
    return 0;
}

int HalXhciReady(void) {
    return gReady;
}

int HalXhciReset(void) {
    volatile UINT32 *Cmd;
    volatile UINT32 *Sts;

    if (!gReady) {
        return -1;
    }
    Cmd = Op32(OP_USBCMD);
    Sts = Op32(OP_USBSTS);

    /* 停跑 */
    *Cmd = (*Cmd) & ~USBCMD_RS;
    if (WaitBitsSet(Sts, USBSTS_HCH) != 0) {
        return -1;
    }
    /* 主机复位 */
    *Cmd = (*Cmd) | USBCMD_HCRST;
    if (WaitBitsClear(Cmd, USBCMD_HCRST) != 0) {
        return -1;
    }
    if (WaitBitsClear(Sts, USBSTS_CNR) != 0) {
        return -1;
    }
    return 0;
}

UINT32 HalXhciPortCount(void) {
    return gReady ? gMaxPorts : 0;
}

int HalXhciPortCcs(UINT32 Port) {
    volatile UINT32 *Sc;
    UINT32 V;

    if (!gReady || Port == 0 || Port > gMaxPorts) {
        return -1;
    }
    Sc = Op32(OP_PORTSC + (Port - 1u) * 0x10u);
    V = *Sc;
    /* 上电（部分控制器 CCS 在 PP=0 时无意义） */
    if ((V & PORTSC_PP) == 0) {
        V = (V & ~PORTSC_CHANGE) | PORTSC_PP;
        *Sc = V;
        V = *Sc;
    }
    return (V & PORTSC_CCS) ? 1 : 0;
}
