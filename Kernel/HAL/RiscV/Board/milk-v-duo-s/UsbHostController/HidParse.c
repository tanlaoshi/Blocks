/*
 * HidParse.c — boot 鼠/键配置解析（bd-duo-usb）
 */
#include "UsbHostController/Private.h"

int ParseBootMouse(UINT8 *Config, UINT16 Total, UINT8 *InterfaceIndex, UINT8 *Endpoint,
                          UINT16 *MaxPacketSize, UINT8 *ScoreOut) {
    UINT16 Offset = 0;
    UINT8 Best = 0;
    UINT8 CurScore = 0;
    UINT8 CurIface = 0;

    *InterfaceIndex = 0;
    *Endpoint = 0;
    *MaxPacketSize = 8;
    if (ScoreOut) {
        *ScoreOut = 0;
    }
    while (Offset + 2 <= Total) {
        UINT8 Length = Config[Offset];
        UINT8 Type = Config[Offset + 1];
        if (Length < 2 || Offset + Length > Total) {
            break;
        }
        if (Type == 4 && Length >= 9) {
            UINT8 Cls = Config[Offset + 5];
            UINT8 Sub = Config[Offset + 6];
            UINT8 Proto = Config[Offset + 7];
            CurScore = 0;
            /* 鼠：Proto≠1（键）；3/1/2 最优 */
            if (Cls == 3 && Proto != 1) {
                CurIface = Config[Offset + 2];
                if (Sub == 1 && Proto == 2) {
                    CurScore = 3;
                } else if (Sub == 1) {
                    CurScore = 2;
                } else {
                    CurScore = 1;
                }
            }
        } else if (Type == 5 && Length >= 7 && CurScore != 0) {
            UINT8 Address = Config[Offset + 2];
            UINT8 Attr = Config[Offset + 3];
            if ((Address & 0x80u) != 0 && (Attr & 3u) == 3u && CurScore > Best) {
                Best = CurScore;
                *InterfaceIndex = CurIface;
                *Endpoint = Address;
                *MaxPacketSize = (UINT16)(Config[Offset + 4] | ((UINT16)Config[Offset + 5] << 8));
                if (Best == 3) {
                    if (ScoreOut) {
                        *ScoreOut = Best;
                    }
                    return 1;
                }
            }
        }
        Offset = (UINT16)(Offset + Length);
    }
    if (ScoreOut) {
        *ScoreOut = Best;
    }
    return Best != 0;
}

/* 对齐 X64 ParseConfig：3/1/1 最优；弱分+像样鼠 → 调用方拒认（G102 假键） */
int ParseBootKeyboard(UINT8 *Config, UINT16 Total, UINT8 *InterfaceIndex, UINT8 *Endpoint,
                             UINT16 *MaxPacketSize, UINT8 *ScoreOut) {
    UINT16 Offset = 0;
    UINT8 Best = 0;
    UINT8 CurScore = 0;
    UINT8 CurIface = 0;

    *InterfaceIndex = 0;
    *Endpoint = 0;
    *MaxPacketSize = 8;
    if (ScoreOut) {
        *ScoreOut = 0;
    }
    while (Offset + 2 <= Total) {
        UINT8 Length = Config[Offset];
        UINT8 Type = Config[Offset + 1];
        if (Length < 2 || Offset + Length > Total) {
            break;
        }
        if (Type == 4 && Length >= 9) {
            UINT8 Cls = Config[Offset + 5];
            UINT8 Sub = Config[Offset + 6];
            UINT8 Proto = Config[Offset + 7];
            CurScore = 0;
            if (Cls == 3 && Sub == 1 && Proto == 1) {
                CurScore = 3;
            } else if (Cls == 3 && Sub == 1 && Proto == 0) {
                CurScore = 2;
            } else if (Cls == 3 && Sub != 1) {
                CurScore = 1;
            }
            CurIface = Config[Offset + 2];
        } else if (Type == 5 && Length >= 7 && CurScore != 0) {
            UINT8 Address = Config[Offset + 2];
            UINT8 Attr = Config[Offset + 3];
            if ((Address & 0x80u) != 0 && (Attr & 3u) == 3u && CurScore > Best) {
                Best = CurScore;
                *InterfaceIndex = CurIface;
                *Endpoint = Address;
                *MaxPacketSize = (UINT16)(Config[Offset + 4] | ((UINT16)Config[Offset + 5] << 8));
                if (Best == 3) {
                    if (ScoreOut) {
                        *ScoreOut = Best;
                    }
                    return 1;
                }
            }
        }
        Offset = (UINT16)(Offset + Length);
    }
    if (ScoreOut) {
        *ScoreOut = Best;
    }
    return Best != 0;
}

void HidSetBootIdle(UINT8 Address, UINT8 InterfaceIndex) {
    UsbSetup Setup;

    Setup.BmRequestType = 0x21;
    Setup.BRequest = 0x0B; /* SET_PROTOCOL boot */
    Setup.WValue = 0;
    Setup.WIndex = InterfaceIndex;
    Setup.WLength = 0;
    (void)ControlMessage(Address, &Setup, 0, 0);
    Setup.BRequest = 0x0A; /* SET_IDLE */
    (void)ControlMessage(Address, &Setup, 0, 0);
}


/*
 * 一口一设备：可鼠、可键、可复合。已绑的不再抢；弱键+像样鼠拒认（NUC 同形）。
 * 返回：绑上至少一路 1；无 HID 0；硬失败 -1。
 */
