/*
 * AudioRegisters.c — `audioreg`：打印 Pi 音频（PWM/CM/GPFSEL4）寄存器真值
 *
 * 仅 aarch64 有板级实现；其它架构打印 "only on rpi-3b"。
 * 用途：区分"寄存器没配"与"配了但孔里没声"。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"

#if defined(__aarch64__)
#include "../../HAL/Arm64/Board/rpi-3b/Audio/AudioPrivate.h"
#endif

void CommandAudioRegisters(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
#if !defined(__aarch64__)
    gConsole.Write("audioreg: only on rpi-3b\n");
#else
    {
        UINT32 v[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

        if (BoardAudioReadRegisters(v) != 0) {
            gConsole.Write("audioreg: read fail\n");
            return;
        }
        gConsole.Write("audioreg: cm_ctl=");
        gConsole.WriteHex32(v[0]);
        gConsole.Write(" cm_div=");
        gConsole.WriteHex32(v[1]);
        gConsole.Write(" pwm_ctl=");
        gConsole.WriteHex32(v[2]);
        gConsole.Write(" pwm_sta=");
        gConsole.WriteHex32(v[3]);
        gConsole.Write("\n");
        gConsole.Write("audioreg: rng1=");
        gConsole.WriteHex32(v[4]);
        gConsole.Write(" dat1=");
        gConsole.WriteHex32(v[5]);
        gConsole.Write(" gpfsel4=");
        gConsole.WriteHex32(v[6]);
        gConsole.Write(" clock_rc=");
        gConsole.WriteHex32(v[7]);
        gConsole.Write("\n");
    }
#endif
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

/* audiotone <hz> <ms> <duty%> —— 扫频/扫占空比，用来区分"路径死"与"参数不对" */
void CommandAudioTone(int Argc, char **Argv) {
#if !defined(__aarch64__)
    (void)Argc;
    (void)Argv;
    gConsole.Write("audiotone: only on rpi-3b\n");
#else
    UINT32 Hz = 1000u;
    UINT32 Ms = 300u;
    UINT32 Duty = 50u;

    if (Argc > 1 && AudioParseNumber(Argv[1], &Hz) != 0) {
        gConsole.Write("audiotone: bad hz\n");
        return;
    }
    if (Argc > 2 && AudioParseNumber(Argv[2], &Ms) != 0) {
        gConsole.Write("audiotone: bad ms\n");
        return;
    }
    if (Argc > 3 && AudioParseNumber(Argv[3], &Duty) != 0) {
        gConsole.Write("audiotone: bad duty\n");
        return;
    }
    if (BoardAudioInitialize() != 0) {
        gConsole.Write("audiotone: init fail\n");
        return;
    }
    if (PiPwmSetTone(Hz, Duty) != 0) {
        gConsole.Write("audiotone: set fail\n");
        return;
    }
    gConsole.Write("audiotone: on hz/ms/duty=");
    gConsole.WriteHex32(Hz);
    gConsole.Write("/");
    gConsole.WriteHex32(Ms);
    gConsole.Write("/");
    gConsole.WriteHex32(Duty);
    gConsole.Write("\n");
    HalDelayMilliseconds(Ms);
    PiPwmSilence();
    gConsole.Write("audiotone: done\n");
#endif
}

/* audiomsen <hz> <ms> —— MSEN/MASH 对照：交替往 FIFO 推 0 与满量程 */
void CommandAudioSerialiser(int Argc, char **Argv) {
#if !defined(__aarch64__)
    (void)Argc;
    (void)Argv;
    gConsole.Write("audiomsen: only on rpi-3b\n");
#else
    UINT32 Hz = 1000u;
    UINT32 Ms = 300u;
    UINT32 Period;
    UINT32 Elapsed = 0u;
    int High = 1;

    if (Argc > 1 && AudioParseNumber(Argv[1], &Hz) != 0) {
        gConsole.Write("audiomsen: bad hz\n");
        return;
    }
    if (Argc > 2 && AudioParseNumber(Argv[2], &Ms) != 0) {
        gConsole.Write("audiomsen: bad ms\n");
        return;
    }
    if (Hz == 0u) {
        gConsole.Write("audiomsen: hz==0\n");
        return;
    }
    if (BoardAudioInitialize() != 0) {
        gConsole.Write("audiomsen: init fail\n");
        return;
    }
    Period = PI_AUDIO_PWM_HZ / Hz;
    if (PiPwmSerialiserEnable(Period) != 0) {
        gConsole.Write("audiomsen: enable fail\n");
        return;
    }
    gConsole.Write("audiomsen: on (MSEN1/2) hz/ms=");
    gConsole.WriteHex32(Hz);
    gConsole.Write("/");
    gConsole.WriteHex32(Ms);
    gConsole.Write("\n");
    while (Elapsed < Ms) {
        PiPwmSerialiserPush(High != 0 ? 0xFFFFFFFFu : 0u);
        High = (High != 0) ? 0 : 1;
        HalDelayMilliseconds(1u);
        Elapsed++;
    }
    PiPwmSerialiserDisable();
    gConsole.Write("audiomsen: done\n");
#endif
}

void ShellCommandsAudioRegistersRegister(void) {
    gConsole.Register("audioreg", "audioreg — Pi PWM/CM/GPFSEL4 寄存器快照", CommandAudioRegisters);
    gConsole.Register("audiotone", "audiotone <hz> <ms> <duty%> — PWM 扫频/扫占空比", CommandAudioTone);
    gConsole.Register("audiomsen", "audiomsen <hz> <ms> — MSEN/MASH 对照", CommandAudioSerialiser);
}
