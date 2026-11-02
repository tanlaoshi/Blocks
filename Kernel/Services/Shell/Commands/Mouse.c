/*
 * Mouse.c — 壳 mouse / mouse watch：无屏板（Duo）点测 HID 鼠
 *
 * Duo 仅一个 Type-A HOST：冷启前插鼠；键与鼠互斥。
 * Buttons：bit0=左 bit1=右 bit2=中（HalDevices.h HAL_MOUSE_REPORT）。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "HalDevices.h"

#define MOUSE_WATCH_STEP_MILLISECONDS 5u
#define MOUSE_WATCH_DEFAULT_MILLISECONDS 15000u
#define MOUSE_MOVE_LOG_MILLISECONDS 100u

static int MouseArgumentEqualIgnoreCase(const char *Argument, const char *Word) {
    const char *A = Word;
    const char *B = Argument;

    if (B == 0) {
        return 0;
    }
    while (*A != 0 && *B != 0) {
        char CharacterA = *A;
        char CharacterB = *B;

        if (CharacterA >= 'A' && CharacterA <= 'Z') {
            CharacterA = (char)(CharacterA - 'A' + 'a');
        }
        if (CharacterB >= 'A' && CharacterB <= 'Z') {
            CharacterB = (char)(CharacterB - 'A' + 'a');
        }
        if (CharacterA != CharacterB) {
            return 0;
        }
        A++;
        B++;
    }
    return (*A == 0 && *B == 0) ? 1 : 0;
}

static int MouseParseMilliseconds(const char *Text, UINT32 *OutMilliseconds) {
    UINT32 Value = 0u;
    int Digits = 0;

    if (Text == 0 || OutMilliseconds == 0) {
        return -1;
    }
    while (*Text >= '0' && *Text <= '9') {
        Value = Value * 10u + (UINT32)(*Text - '0');
        Digits++;
        Text++;
    }
    if (Digits == 0 || *Text != 0 || Value == 0u) {
        return -1;
    }
    *OutMilliseconds = Value;
    return 0;
}

static void MouseWriteUsage(void) {
    gConsole.Write(
        "mouse | mouse watch [ms] [move]\n"
        "  Duo: plug Type-A mouse before boot (one HOST; no hotplug)\n");
}

static void MouseWriteButtonEdge(UINT8 PreviousButtons, UINT8 CurrentButtons, UINT8 Bit,
                                 const char *Name) {
    int WasDown = (PreviousButtons & Bit) != 0;
    int IsDown = (CurrentButtons & Bit) != 0;

    if (WasDown == IsDown) {
        return;
    }
    gConsole.Write("mouse: ");
    gConsole.Write(Name);
    gConsole.Write(IsDown ? " down\n" : " up\n");
}

static void MouseCommandInfo(void) {
    gConsole.Write(HalMousePresent() ? "mouse: present\n" : "mouse: none\n");
}

static void MouseCommandWatch(int Argc, char **Argv) {
    UINT32 BudgetMilliseconds = MOUSE_WATCH_DEFAULT_MILLISECONDS;
    UINT32 ElapsedMilliseconds = 0u;
    UINT32 LastMoveLogMilliseconds = 0u;
    UINT32 Events = 0u;
    UINT8 PreviousButtons = 0u;
    int LogMove = 0;
    int HaveButtons = 0;
    UINT32 LastX = 0u;
    UINT32 LastY = 0u;
    int ArgIndex;

    for (ArgIndex = 2; ArgIndex < Argc; ArgIndex++) {
        if (MouseArgumentEqualIgnoreCase(Argv[ArgIndex], "move")) {
            LogMove = 1;
        } else if (MouseParseMilliseconds(Argv[ArgIndex], &BudgetMilliseconds) != 0) {
            MouseWriteUsage();
            return;
        }
    }

    if (!HalMousePresent()) {
        gConsole.Write(
            "mouse: none — cold-plug Type-A mouse, reboot; keyboard shares the only HOST\n");
        return;
    }

    gConsole.Write("mouse: watch start ms=");
    gConsole.WriteHex32(BudgetMilliseconds);
    gConsole.Write(LogMove ? " (+move)\n" : "\n");

    while (ElapsedMilliseconds < BudgetMilliseconds) {
        HAL_MOUSE_REPORT Report;

        HalDelayMilliseconds(MOUSE_WATCH_STEP_MILLISECONDS);
        ElapsedMilliseconds += MOUSE_WATCH_STEP_MILLISECONDS;
        HalInputPoll();
        while (HalMouseDequeue(&Report)) {
            if (!HaveButtons) {
                PreviousButtons = Report.Buttons;
                LastX = Report.X;
                LastY = Report.Y;
                HaveButtons = 1;
            }
            MouseWriteButtonEdge(PreviousButtons, Report.Buttons, 1u, "left");
            MouseWriteButtonEdge(PreviousButtons, Report.Buttons, 2u, "right");
            MouseWriteButtonEdge(PreviousButtons, Report.Buttons, 4u, "middle");
            if (Report.Buttons != PreviousButtons) {
                Events++;
            }
            if (LogMove &&
                (Report.X != LastX || Report.Y != LastY) &&
                (ElapsedMilliseconds - LastMoveLogMilliseconds) >=
                    MOUSE_MOVE_LOG_MILLISECONDS) {
                gConsole.Write("mouse: move x=");
                gConsole.WriteHex32(Report.X);
                gConsole.Write(" y=");
                gConsole.WriteHex32(Report.Y);
                if (Report.Wheel != 0) {
                    gConsole.Write(" wheel=");
                    gConsole.WriteHex32((UINT32)(UINT8)Report.Wheel);
                }
                gConsole.Write("\n");
                LastMoveLogMilliseconds = ElapsedMilliseconds;
                LastX = Report.X;
                LastY = Report.Y;
                Events++;
            } else {
                LastX = Report.X;
                LastY = Report.Y;
            }
            PreviousButtons = Report.Buttons;
        }
    }

    gConsole.Write("mouse: watch done events=");
    gConsole.WriteHex32(Events);
    gConsole.Write("\n");
}

static void CommandMouse(int Argc, char **Argv) {
    if (Argc <= 1) {
        MouseCommandInfo();
        return;
    }
    if (MouseArgumentEqualIgnoreCase(Argv[1], "watch")) {
        MouseCommandWatch(Argc, Argv);
        return;
    }
    MouseWriteUsage();
}

void ShellCommandsMouseRegister(void) {
    gConsole.Register("mouse", "mouse | mouse watch [ms] [move]", CommandMouse);
}
