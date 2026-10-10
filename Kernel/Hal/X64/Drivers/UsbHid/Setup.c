/*
 * Setup.c — 配置描述符解析 + SetConfig + 中断 EP
 */
#include "Private.h"

static UINT8 FsInterval(UINT8 BInterval) {
    UINT8 Log2 = 0;
    UINT8 V = BInterval ? BInterval : 1u;
    while (V > 1u) {
        V >>= 1;
        Log2++;
    }
    return (UINT8)(Log2 + 3u);
}

static int ParseHid(UINT8 *Cfg, UINT16 Total, int WantKbd, HidDev *D) {
    UINT16 Off = 0;
    UINT8 Best = 0;
    UINT8 CurScore = 0;
    UINT8 CurIface = 0;
    UINT8 CurProto = 0;

    D->Iface = 0;
    D->EpAddr = 0;
    D->Mps = 8;
    D->Interval = 10;
    D->Proto = 0xFF;
    D->Absolute = 0;

    while (Off + 2u <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1u];
        if (Len < 2u || Off + Len > Total) {
            break;
        }
        if (Type == 4u && Len >= 9u) {
            UINT8 Class = Cfg[Off + 5u];
            UINT8 Sub = Cfg[Off + 6u];
            UINT8 Proto = Cfg[Off + 7u];
            CurScore = 0;
            CurIface = Cfg[Off + 2u];
            CurProto = Proto;
            if (WantKbd) {
                if (Class == 3u && Sub == 1u && Proto == 1u) {
                    CurScore = 3;
                } else if (Class == 3u && Sub == 1u && Proto == 0u) {
                    CurScore = 2;
                }
            } else if (Class == 3u && Proto != 1u) {
                if (Sub == 1u && Proto == 2u) {
                    CurScore = 3;
                } else if (Sub == 1u) {
                    CurScore = 2;
                } else {
                    CurScore = 1;
                }
            }
        } else if (Type == 5u && Len >= 7u && CurScore != 0) {
            UINT8 Addr = Cfg[Off + 2u];
            UINT8 Attr = Cfg[Off + 3u];
            if ((Addr & 0x80u) && ((Attr & 0x03u) == 0x03u) && CurScore > Best) {
                Best = CurScore;
                D->Iface = CurIface;
                D->EpAddr = Addr;
                D->Mps = (UINT16)(Cfg[Off + 4u] | (Cfg[Off + 5u] << 8));
                D->Interval = Cfg[Off + 6u];
                D->Proto = CurProto;
                if (Best == 3u) {
                    break;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (Best == 0) {
        return 0;
    }
    if (!WantKbd && D->Proto != 2u) {
        D->Absolute = 1;
    }
    return 1;
}

int FinishHid(HidDev *D, int WantKbd) {
    SetupPkt Setup;
    UINT16 Total;
    UINT8 ConfigVal;
    UINT32 *Slot;
    UINT32 *Ep;
    UINT64 Deq;
    UINT8 Interval;
    UINT16 Mps;
    UINT8 EpNum;
    UINT8 In;

    gXferDev = D;
    if (GetDesc(0x0100u, 8, gCtrlBuf) < 0 || GetDesc(0x0100u, 18, gCtrlBuf) < 0) {
        return -1;
    }
    if (GetDesc(0x0200u, 9, gCtrlBuf) < 0) {
        return -1;
    }
    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9u) {
        Total = 9;
    }
    if (Total > 512u) {
        Total = 512;
    }
    if (GetDesc(0x0200u, Total, gCtrlBuf) < 0) {
        return -1;
    }
    if (!ParseHid(gCtrlBuf, Total, WantKbd, D)) {
        return -1;
    }
    ConfigVal = gCtrlBuf[5] ? gCtrlBuf[5] : 1u;
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 0x09;
    Setup.WValue = ConfigVal;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlXfer(&Setup, 0) < 0) {
        return -1;
    }
    if (D->Proto == 1u || D->Proto == 2u) {
        Setup.BmRequestType = 0x21;
        Setup.BRequest = 0x0B;
        Setup.WValue = 0;
        Setup.WIndex = D->Iface;
        Setup.WLength = 0;
        (void)ControlXfer(&Setup, 0);
    }
    Setup.BmRequestType = 0x21;
    Setup.BRequest = 0x0A;
    Setup.WValue = 0;
    Setup.WIndex = D->Iface;
    Setup.WLength = 0;
    (void)ControlXfer(&Setup, 0);

    EpNum = D->EpAddr & 0x0Fu;
    In = (D->EpAddr & 0x80u) ? 1u : 0u;
    D->IntrDci = (UINT32)EpNum * 2u + In;
    Mps = D->Mps;
    if (Mps == 0 || Mps > 64u) {
        Mps = 8;
    }
    D->Mps = Mps;

    Zero(gInCtx, INCTX_BYTES);
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << D->IntrDci);
    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (D->IntrDci << 27) | ((UINT32)D->Speed << 20);
    Slot[1] = (UINT32)D->Port << 16;
    InitRing(D->IntrRing, &D->Intr, RING_SIZE);
    Ep = (UINT32 *)(void *)InEp(D->IntrDci);
    Interval = (D->Speed >= 3u)
                   ? (UINT8)((D->Interval > 0) ? (D->Interval - 1u) : 0u)
                   : FsInterval(D->Interval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    Deq = Phys(D->IntrRing) | 1ULL;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);
    if (Command(Phys(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(D->SlotId), 0) < 0) {
        return -1;
    }
    QueueIntr(D);
    return 0;
}

