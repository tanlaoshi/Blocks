/*
 * HidParse.c — PR-S-xhcihid-1：配置描述符解析（键/鼠）
 * 从 Hid.c 原样搬家；不改语义。HID 全局仍在 Xhci.c。
 */
#include "UsbHid/Private.h"

int ParseConfig(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                       UINT8 *Interface, UINT8 *EndpointAddress, UINT16 *MaxPacketSize, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 CurScore = 0;
    UINT8 BestScore = 0;
    UINT8 CurInterface = 0;
    *Interface = 0;
    *EndpointAddress = 0;
    *MaxPacketSize = 8;
    *Interval = 10;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Class = Cfg[Off + 5];
            UINT8 Sub = Cfg[Off + 6];
            UINT8 Proto = Cfg[Off + 7];
            CurScore = 0;
            /* 3/1/1 boot keyboard 最优；3/1/0 次之；3/0/x 亦试（真机常见） */
            if (Class == 3 && Sub == 1 && Proto == 1) {
                CurScore = 3;
            } else if (Class == 3 && Sub == 1 && Proto == 0) {
                CurScore = 2;
            } else if (Class == 3 && Sub != 1) {
                CurScore = 1;
            }
            CurInterface = Cfg[Off + 2];
        } else if (Type == 5 && Len >= 7 && CurScore) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03) && CurScore > BestScore) {
                BestScore = CurScore;
                *Interface = CurInterface;
                *EndpointAddress = Addr;
                *MaxPacketSize = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                *Interval = Cfg[Off + 6];
                (void)Speed;
                if (BestScore == 3) {
                    return 1;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    gKeyboardParseScore = BestScore;
    return BestScore != 0;
}

/*
 * 真机：罗技 G102 等游戏鼠常带额外 HID（媒体/宏，3/0/x 或 3/1/0），
 * 旧逻辑会当成「键盘」占 slot，再把同设备 boot 鼠绑成 composite →
 * 真键盘口永远轮不到，且假键盘 k=0、鼠却丝滑。
 * 若本设备「键盘分」<3 且已有像样鼠标接口 → 不当键盘，留给 MousePortInitialize。
 */
int RealPcRejectMouseExtraAsKeyboard(UINT16 Total, UINT8 Speed) {
    UINT8 MIface = 0, MouseEndpoint = 0, MIv = 10;
    UINT16 MouseMaxPacketSize = 8;

    if (HalCpuIsHypervisor()) {
        return 0;
    }
    if (gKeyboardParseScore >= 3) {
        return 0;
    }
    if (!ParseConfigMouse(gControlBuffer, Total, Speed, &MIface, &MouseEndpoint, &MouseMaxPacketSize, &MIv)) {
        return 0;
    }
    if (gMouseParseScore < 2) {
        return 0;
    }
    BootLog("Boot: XHCI skip mouse+extraHID as kbd\n");
    {
        char Line[48];
        int n = 0;
        const char *P = "Boot: XHCI kbd-score=";
        while (*P && n < 24) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + (gKeyboardParseScore % 10));
        P = " mouse-score=";
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + (gMouseParseScore % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    return 1;
}

int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                            UINT8 *Interface, UINT8 *EndpointAddress, UINT16 *MaxPacketSize, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 FoundInterface = 0;
    UINT8 BestScore = 0;
    UINT8 BestInterface = 0;
    UINT8 BestEndpoint = 0;
    UINT16 BestMaxPacketSize = 8;
    UINT8 BestInterval = 10;
    UINT8 BestProto = 0xFF;
    UINT8 CurProto = 0;
    UINT8 CurSub = 0;
    UINT8 CurScore = 0;

    *Interface = 0;
    *EndpointAddress = 0;
    *MaxPacketSize = 8;
    *Interval = 10;

    /*
     * 评分：boot mouse (3/1/2)=3；boot 子类 Proto0 (3/1/0)=2；其它 HID 非键盘=1。
     * 真机曾把 Proto=0 的附加 HID（媒体键等）当成鼠标 → EP 无报告 m=0。
     */
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Class = Cfg[Off + 5];
            CurSub = Cfg[Off + 6];
            CurProto = Cfg[Off + 7];
            CurScore = 0;
            FoundInterface = 0;
            if (Class == 3 && CurProto != 1) {
                FoundInterface = 1;
                *Interface = Cfg[Off + 2];
                if (CurSub == 1 && CurProto == 2) {
                    CurScore = 3;
                } else if (CurSub == 1) {
                    CurScore = 2;
                } else {
                    CurScore = 1;
                }
            }
        } else if (Type == 5 && Len >= 7 && FoundInterface && CurScore != 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03) && CurScore > BestScore) {
                BestScore = CurScore;
                BestProto = CurProto;
                BestInterface = *Interface;
                BestEndpoint = Addr;
                BestMaxPacketSize = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                BestInterval = Cfg[Off + 6];
                if (BestScore == 3) {
                    break;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    (void)Speed;
    if (BestScore == 0) {
        return 0;
    }
    *Interface = BestInterface;
    *EndpointAddress = BestEndpoint;
    *MaxPacketSize = BestMaxPacketSize;
    *Interval = BestInterval;
    gMouseInterfaceProto = BestProto;
    gMouseParseScore = BestScore;
    gMouseAbsolute = (BestProto != 2 && HalCpuIsHypervisor()) ? 1 : 0;
    return 1;
}
