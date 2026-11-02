/*
 * AudioRegisters.c — `audioreg` / `audiotone` / `audiomsen`：Pi PWM 取证
 *
 * 板符号一律 weak（对表 Bluetooth.c / Wifi.c）：CI `build.sh arm64` 默认
 * BOARD=virt，无 rpi-3b Audio 实现；硬链会让 Actions 挂掉。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"

#define PI_AUDIO_PWM_CLOCK_HZ 10000000u /* 与 rpi-3b AudioPrivate.h 同口径 */

int BoardAudioReadRegisters(UINT32 Out[8]) __attribute__((weak));
int BoardAudioInitialize(void) __attribute__((weak));
int PiPwmSetTone(UINT32 FrequencyHz, UINT32 DutyPercent) __attribute__((weak));
void PiPwmSilence(void) __attribute__((weak));
int PiPwmSerialiserEnable(UINT32 Period) __attribute__((weak));
void PiPwmSerialiserPush(UINT32 Value) __attribute__((weak));
void PiPwmSerialiserDisable(void) __attribute__((weak));

static int PiAudioBoardPresent(void) {
    return BoardAudioReadRegisters != 0 && BoardAudioInitialize != 0 &&
           PiPwmSetTone != 0 && PiPwmSilence != 0 &&
           PiPwmSerialiserEnable != 0 && PiPwmSerialiserPush != 0 &&
           PiPwmSerialiserDisable != 0;
}

static int AudioParseNumber(const char *Text, UINT32 *Out) {
    UINT32 Value = 0u;
    UINT32 Digits = 0u;

    if (Text == 0 || Out == 0) {
        return -1;
    }
    while (*Text >= '0' && *Text <= '9') {
        Value = Value * 10u + (UINT32)(*Text - '0');
        Digits++;
        Text++;
    }
    if (Digits == 0u || *Text != 0) {
        return -1;
    }
    *Out = Value;
    return 0;
}

void CommandAudioRegisters(int Argc, char **Argv) {
    UINT32 Registers[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    (void)Argc;
    (void)Argv;
    if (!PiAudioBoardPresent()) {
        gConsole.Write("audioreg: only on rpi-3b\n");
        return;
    }
    if (BoardAudioReadRegisters(Registers) != 0) {
        gConsole.Write("audioreg: read fail\n");
        return;
    }
    gConsole.Write("audioreg: cm_ctl=");
    gConsole.WriteHex32(Registers[0]);
    gConsole.Write(" cm_div=");
    gConsole.WriteHex32(Registers[1]);
    gConsole.Write(" pwm_ctl=");
    gConsole.WriteHex32(Registers[2]);
    gConsole.Write(" pwm_sta=");
    gConsole.WriteHex32(Registers[3]);
    gConsole.Write("\n");
    gConsole.Write("audioreg: rng1=");
    gConsole.WriteHex32(Registers[4]);
    gConsole.Write(" dat1=");
    gConsole.WriteHex32(Registers[5]);
    gConsole.Write(" gpfsel4=");
    gConsole.WriteHex32(Registers[6]);
    gConsole.Write(" clock_rc=");
    gConsole.WriteHex32(Registers[7]);
    gConsole.Write("\n");
}

/* audiotone <hz> <ms> <duty%> —— 扫频/扫占空比 */
void CommandAudioTone(int Argc, char **Argv) {
    UINT32 FrequencyHz = 1000u;
    UINT32 Milliseconds = 300u;
    UINT32 DutyPercent = 50u;

    if (!PiAudioBoardPresent()) {
        (void)Argc;
        (void)Argv;
        gConsole.Write("audiotone: only on rpi-3b\n");
        return;
    }
    if (Argc > 1 && AudioParseNumber(Argv[1], &FrequencyHz) != 0) {
        gConsole.Write("audiotone: bad hz\n");
        return;
    }
    if (Argc > 2 && AudioParseNumber(Argv[2], &Milliseconds) != 0) {
        gConsole.Write("audiotone: bad ms\n");
        return;
    }
    if (Argc > 3 && AudioParseNumber(Argv[3], &DutyPercent) != 0) {
        gConsole.Write("audiotone: bad duty\n");
        return;
    }
    if (BoardAudioInitialize() != 0) {
        gConsole.Write("audiotone: init fail\n");
        return;
    }
    if (PiPwmSetTone(FrequencyHz, DutyPercent) != 0) {
        gConsole.Write("audiotone: set fail\n");
        return;
    }
    gConsole.Write("audiotone: on hz/ms/duty=");
    gConsole.WriteHex32(FrequencyHz);
    gConsole.Write("/");
    gConsole.WriteHex32(Milliseconds);
    gConsole.Write("/");
    gConsole.WriteHex32(DutyPercent);
    gConsole.Write("\n");
    HalDelayMilliseconds(Milliseconds);
    PiPwmSilence();
    gConsole.Write("audiotone: done\n");
}

/* audiomsen <hz> <ms> —— MSEN/MASH 对照 */
void CommandAudioSerialiser(int Argc, char **Argv) {
    UINT32 FrequencyHz = 1000u;
    UINT32 Milliseconds = 300u;
    UINT32 Period;
    UINT32 Elapsed = 0u;
    int High = 1;

    if (!PiAudioBoardPresent()) {
        (void)Argc;
        (void)Argv;
        gConsole.Write("audiomsen: only on rpi-3b\n");
        return;
    }
    if (Argc > 1 && AudioParseNumber(Argv[1], &FrequencyHz) != 0) {
        gConsole.Write("audiomsen: bad hz\n");
        return;
    }
    if (Argc > 2 && AudioParseNumber(Argv[2], &Milliseconds) != 0) {
        gConsole.Write("audiomsen: bad ms\n");
        return;
    }
    if (FrequencyHz == 0u) {
        gConsole.Write("audiomsen: hz==0\n");
        return;
    }
    if (BoardAudioInitialize() != 0) {
        gConsole.Write("audiomsen: init fail\n");
        return;
    }
    Period = PI_AUDIO_PWM_CLOCK_HZ / FrequencyHz;
    if (PiPwmSerialiserEnable(Period) != 0) {
        gConsole.Write("audiomsen: enable fail\n");
        return;
    }
    gConsole.Write("audiomsen: on (MSEN1/2) hz/ms=");
    gConsole.WriteHex32(FrequencyHz);
    gConsole.Write("/");
    gConsole.WriteHex32(Milliseconds);
    gConsole.Write("\n");
    while (Elapsed < Milliseconds) {
        PiPwmSerialiserPush(High != 0 ? 0xFFFFFFFFu : 0u);
        High = (High != 0) ? 0 : 1;
        HalDelayMilliseconds(1u);
        Elapsed++;
    }
    PiPwmSerialiserDisable();
    gConsole.Write("audiomsen: done\n");
}

void ShellCommandsAudioRegistersRegister(void) {
    gConsole.Register("audioreg", "audioreg — Pi PWM/CM/GPFSEL4 寄存器快照",
                      CommandAudioRegisters);
    gConsole.Register("audiotone", "audiotone <hz> <ms> <duty%> — PWM 扫频/扫占空比",
                      CommandAudioTone);
    gConsole.Register("audiomsen", "audiomsen <hz> <ms> — MSEN/MASH 对照",
                      CommandAudioSerialiser);
}
