/*
 * Serial.c — 串口壳循环 + 单字符喂入（PR-BOX-1）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Hal.h"
#include "HalDevices.h"
#include "HIDKeyboard.h"
#include "LwIp.h"
#include "ShellCommands.h"

static void SerialFeedChar(char C, int *SkipLf) {
    int Skip;

    if (gConsole.StdinUserHold()) {
        /* 仍回显：CoolTerm 常关 Local Echo；Hold 时静默像死机 */
        if (C >= 32 && C <= 126) {
            HalConsolePutChar(C);
        } else if (C == '\r' || C == '\n') {
            HalConsoleWriteSerial("\n");
        } else if (C == '\b' || C == 127) {
            HalConsoleBackspaceSerial();
        }
        gConsole.StdinPut(C);
        return;
    }
    Skip = (SkipLf != 0) ? *SkipLf : 0;
    if (gConsole.HistIgnoreRx()) {
        if (C == '\r') {
            if (SkipLf) {
                *SkipLf = 1;
            }
            gConsole.OnEnterEx(1);
            gConsole.HistClearIgnoreRx();
        } else if (C == '\n') {
            if (Skip) {
                if (SkipLf) {
                    *SkipLf = 0;
                }
            } else {
                gConsole.OnEnterEx(1);
                gConsole.HistClearIgnoreRx();
            }
        } else if (C == 3) {
            gConsole.HistClearIgnoreRx();
            ShellOnInterrupt();
        } else {
            (void)gConsole.HistFeedAnsi(C, 1);
        }
        return;
    }
    if (C == '\r') {
        if (SkipLf) {
            *SkipLf = 1;
        }
        gConsole.OnEnterEx(1);
        return;
    }
    if (C == '\n') {
        if (Skip) {
            if (SkipLf) {
                *SkipLf = 0;
            }
        } else {
            gConsole.OnEnterEx(1);
        }
        return;
    }
    if (SkipLf) {
        *SkipLf = 0;
    }
    if (C == '\b' || C == 127) {
        gConsole.OnBackspaceEx(1);
    } else if (C == 3) {
        ShellOnInterrupt();
    } else if (!gConsole.HistFeedAnsi(C, 1) && C >= 32 && C <= 126) {
        gConsole.OnCharEx(C, 1);
    }
}

/* PR-A9/V3：virt 串口 + virtio-input 键盘 */
static void SerialRun(void) {
    static HAL_KEYBOARD_REPORT Prev;
    HAL_KEYBOARD_REPORT Report;
    static int SkipLf;

    if (HalPlatformIsVirtSerialConsole()) {
        HalConsoleWriteSerial(
            "virt: serial shell (help/mem/ps/halt; kbd via virtio-input)\n");
    } else {
        HalConsoleWriteSerial("serial shell (help/mem/ps/halt)\n");
    }
    gConsole.AnnounceBootReady();
    for (;;) {
        HalCpuHalt();
        HalInputPoll();
        /*
         * ConsoleOnly 无 Worker：泵网 + DHCP Step（net-6）。
         * 对表 X64：epoch→自动 DHCP（Duo/无 FB Pi）；再 Step。
         */
        LwIpDhcpOnNicEpoch();
        HalNetPoll();
        if (LwIpDhcpJobBusy()) {
            (void)LwIpDhcpStep();
        }
        if (HalPowerButtonPressed()) {
            HalConsoleWriteSerial("power button -> shutdown\n");
            HalCpuShutdown();
        }
        {
            int n = 0;
            while (HalSerialDataReady() && n < 256) {
                char C = HalSerialReadChar();
                n++;
                gConsole.SerialFeedChar(C, &SkipLf);
            }
        }
        while (HalKeyboardDequeue(&Report)) {
            int i;
            for (i = 0; i < 6; i++) {
                UINT8 Key = Report.KeyCode[i];
                int Was = 0;
                int j;
                if (Key == 0) {
                    continue;
                }
                for (j = 0; j < 6; j++) {
                    if (Prev.KeyCode[j] == Key) {
                        Was = 1;
                        break;
                    }
                }
                if (Was) {
                    continue;
                }
                /*
                 * 与 Tasks/Hid.c FeedHid 同形：exec 占 stdin 时键进环，
                 * 勿走 shell 行（否则退格 gLen=0 空操作，像「没处理」）。
                 */
                if (gConsole.StdinUserHold()) {
                    if (Key == HID_KEY_ENTER) {
                        gConsole.StdinPut('\n');
                    } else if (Key == HID_KEY_BACKSPACE || Key == HID_KEY_DELETE) {
                        gConsole.StdinPut('\b');
                    } else if (Key == HID_KEY_CAPSLOCK) {
                        HIDKeyboardToggleCapsLock();
                        (void)HalKeyboardSetLeds(HIDKeyboardGetLeds());
                    } else if (Key == HID_KEY_C &&
                               (Report.ModifierKeys &
                                (HID_MOD_LCTRL | HID_MOD_RCTRL)) != 0) {
                        ShellOnInterrupt();
                    } else {
                        char C = HIDKeyCodeToASCII(Key, Report.ModifierKeys);
                        if (C) {
                            gConsole.StdinPut(C);
                        }
                    }
                    continue;
                }
                if (Key == HID_KEY_ENTER) {
                    gConsole.OnEnter();
                } else if (Key == HID_KEY_BACKSPACE || Key == HID_KEY_DELETE) {
                    gConsole.OnBackspace();
                } else if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                    gConsole.OnHistHid(Key, 0);
                } else if (Key == HID_KEY_CAPSLOCK) {
                    /* ConsoleOnly 无 Input 任务，须在此切 Caps + 灯 */
                    HIDKeyboardToggleCapsLock();
                    (void)HalKeyboardSetLeds(HIDKeyboardGetLeds());
                } else if (Key == HID_KEY_C &&
                           (Report.ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL)) != 0) {
                    ShellOnInterrupt();
                } else {
                    char C = HIDKeyCodeToASCII(Key, Report.ModifierKeys);
                    if (C) {
                        gConsole.OnChar(C);
                    }
                }
            }
            Prev = Report;
        }
        HalCpuRelax();
    }
}

void ConsolePublishSerial(CONSOLE_OPS *Ops) {
    Ops->SerialFeedChar = SerialFeedChar;
    Ops->SerialRun = SerialRun;
}
