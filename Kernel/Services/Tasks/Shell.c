/*
 * Shell.c — ShellTask 环（runtime-e-shell + gui-g6-present）
 *
 * ready / blocks>（谁打、谁勿抢）：
 *   Worker 泵到末档 AnnounceBootReady（一次）→ ready 文案 + blocks> + ShellOwn
 *   ShellTask 启动勿 Prompt（否则夹在 iwl/dhcp 黄字中间）
 *   SerialRun（ConsoleOnly/virt）也可 Announce；同 once 门闩
 *
 * 一圈序：
 *   1 HID dequeue（只取键；稳态 drain 在 YieldForPollInput）
 *   2 串口 RX — 必须在 Present 前（16550 FIFO@115200≈1.4ms 满；后抽粘贴截断）
 *     分流：StdinUserHold / Settings·Files·Edit·Tty 焦点 → 不进 shell 行
 *   3 HalVideoPresent（!GuiPresentBlocked）— 只推进一轮脏（非 Flush）
 *   4 LwIp|Net/Tcp → UDP 回显
 *   5 YieldForPollInput
 *
 * Present 契约（G6；细节 Present.c 头）：
 *   · 此处用 Present 不用 Flush：Shell 环要短，Partial 留给后续圈/Compose Flush
 *   · GuiPresentBlocked（拖/ComposeBusy）时跳过，避免与 Gui 抢 GOP
 *   · 合成收官、关窗、主题全屏 → Compose 路径 PresentFlush（Busy 内）
 * 不调 GuiPollMouse（runtime-a-poll）。四环 runtime-4；Worker 序 runtime-d-worker。
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "ConsolePrivate.h"
#include "Gui.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"
#include "ShellCommands.h"
#include "SerialLog.h"

/* CoolTerm 常发 CR+LF：两次 Enter → 双 blocks>；吞掉紧跟 CR 的 LF */
static int gSerialSkipLf;

void ShellTask(void) {
    HAL_KEYBOARD_REPORT Report = {0};
    HAL_KEYBOARD_REPORT Previous = {0};
    DebugWrite("shell task running (preemptive)\n");
    /* ready/blocks>：见文件头；此处勿 Prompt */
    for (;;) {
        /*
         * 真机 xHCI 为 poll（无 MSI）：必须先 Drain/取键再 hlt。
         * 旧序先 Halt → 仅靠定时器偶发唤醒，事件环易在 gui 启动后溢满假死。
         * PR-S-input-drain：drain 已移交 YieldForPollInput（稳态）+ StoreIoBreath（长 IO）；
         * 此处只 dequeue。
         */
        while (HalKeyboardDequeue(&Report)) {
            FeedHid(&Report, &Previous);
            Previous = Report;
        }
        /*
         * PR-19 修复：按住方向键的重复节拍。此处在 for(;;) 每圈都走——queue 空、
         * 没有新报告时也会执行；键状态由 FeedHid 记下的 gLastReport 提供。
         * 勿只放 TasksPumpKeyboard：那只在用户态 poll 时跑，Edit 内核窗按住
         * 期间没有事件 → 永远不 tick（真机「只动一行」）。
         */
        TasksHidRepeatTick();
        /* 鼠标泵仅 GuiTask（runtime-a-poll）；此处勿 GuiPollMouse */
        /*
         * COM1 RX → Shell / 用户 stdin 环（CoolTerm 遥控）。
         * 必须在 HalVideoPresent 之前抽：Present 可达数 ms，16550 FIFO(16)
         * 在 115200 下约 1.4ms 就满；旧序 Present→RX 时粘贴 dbset 必截断。
         * MaxRx 仍限一轮喂 shell 量；HW→软环由 SerialRxPump（定时器）兜底。
         */
        {
            int n = 0;
            int MaxRx = 256;
            while (HalSerialDataReady() && n < MaxRx) {
                char C = HalSerialReadChar();
                n++;
                /*
                 * exec 中：勿喂 shell，泄进 stdin 环供 read(0)；
                 * 若直接跳过不抽 RX，CoolTerm 粘贴同样会丢字。
                 */
                if (gConsole.StdinUserHold()) {
                    gConsole.StdinPut(C);
                    continue;
                }
                if (SettingsUiIsFocused()) {
                    /* 仅截 ESC/数字；其余字母仍进串口壳（否则 CoolTerm 像死） */
                    if (C == 0x1B) {
                        SettingsUiOnEscape();
                        continue;
                    }
                    if (C >= '0' && C <= '9') {
                        SettingsUiOnDigit(C);
                        continue;
                    }
                }
                if (FilesUiIsFocused()) {
                    if (C == 0x1B) {
                        FilesUiOnEscape();
                    } else if (C == '\r' || (C == '\n' && !gSerialSkipLf)) {
                        if (C == '\r') {
                            gSerialSkipLf = 1;
                        }
                        FilesUiOnEnter();
                    } else if (C == '\n' && gSerialSkipLf) {
                        gSerialSkipLf = 0;
                    } else if (C == '\b' || C == 127) {
                        FilesUiOnBackspace();
                    } else if (C >= 32 && C <= 126) {
                        FilesUiOnChar(C);
                    }
                    continue;
                }
                if (EditUiIsFocused()) {
                    if (C == 0x1B) {
                        EditUiOnEscape();
                    } else if (C == '\r' || (C == '\n' && !gSerialSkipLf)) {
                        if (C == '\r') {
                            gSerialSkipLf = 1;
                        }
                        EditUiOnEnter();
                    } else if (C == '\n' && gSerialSkipLf) {
                        gSerialSkipLf = 0;
                    } else if (C == '\b' || C == 127) {
                        EditUiOnBackspace();
                    } else if (C == 19) {
                        EditUiSave();
                    } else if (C >= 32 && C <= 126) {
                        EditUiOnChar(C);
                    }
                    continue;
                }
                if (TtyUiIsFocused()) {
                    TtyUiOnRxChar(C);
                    continue;
                }
                gConsole.SerialFeedChar(C, &gSerialSkipLf);
            }
        }
        /*
         * PR-G-shell-present：打字回显经 EchoMark 跳过逐键 Present，
         * 本处合并提交脏区（亦续传上次半途 gDirty）。virt/真机同路径。
         */
        /* 拖帧合成中后缓冲是半成品；这一刷会整块再贴，闪且卡 */
        if (!GuiPresentBlocked()) {
            HalVideoPresent();
        }
#ifdef LWIP_ENABLE
        /*
         * NO_SYS：Shell@AP 与 Worker@BSP 均可 LwIpService；gLwIpLock 串行。
         * 勿在 Job busy 时停泵——窗/Worker 上的 HttpGet 仍依赖 Shell 侧轮询 RX。
         *
         * DHCP Job：必须 Step（Adopt + WriteToJobShell + ReleasePrompt）。
         * 仅 Service 时 lwIP 可能已拿到地址，但壳一直停在「queued」（Pi 有 FB
         * 走 ShellTask、非 SerialRun；Worker 无定时 IRQ 时更易饿死 Step）。
         */
        /*
         * 自动 DHCP：对表 X64 Worker epoch 观察。Pi 有 FB 时 Worker 无 CNTV IRQ
         * 易饿死，Shell 环必跑，故在此同调 LwIpDhcpOnNicEpoch。
         */
        LwIpDhcpOnNicEpoch();
        if (LwIpDhcpJobBusy()) {
            (void)LwIpDhcpStep();
        } else if (LwIpActive()) {
            LwIpService();
        } else {
            HalNetPoll();
            TcpPoll();
        }
#else
        HalNetPoll();
        TcpPoll();
#endif
        if (HalPowerButtonPressed()) {
            LogBoot("Boot: Power Button -> Shutdown\n");
            HalCpuShutdown();
        }
        {
            UDP_DATAGRAM Dg;
            int (*RecvFn)(UDP_DATAGRAM *) = UdpRecv;
#ifdef LWIP_ENABLE
            if (LwIpActive()) {
                RecvFn = LwIpUdpRecv;
            }
#endif
            while (RecvFn(&Dg)) {
                char IpBuf[20];
                UINTN i;
                HalNetFormatIp(Dg.SrcIp, IpBuf, sizeof(IpBuf));
                gConsole.Write("udp from ");
                gConsole.Write(IpBuf);
                gConsole.Write(":");
                gConsole.WriteHex32(Dg.SrcPort);
                gConsole.Write(" ");
                for (i = 0; i < Dg.Len; i++) {
                    char C = (char)Dg.Data[i];
                    if (C >= 32 && C <= 126) {
                        gConsole.OnChar(C);
                    }
                }
                gConsole.Write("\n");
            }
        }
        YieldForPollInput();
    }
}
