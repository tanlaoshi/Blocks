/*
 * DuoAudio.c — `duoaudio regs|fifo|L|R|b|lr`：Duo I2S3/DAC 取证与左右试听
 *
 * 板符号一律 weak（对表 Wifi.c / AudioRegisters.c）：CI `build.sh riscv` 默认
 * BOARD=virt，无 milk-v-duo-s Audio；硬链会让 Actions 挂掉。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"

#define DUO_AUDIO_SHELL_AMPLITUDE 8000

int DuoAudioInitialize(void) __attribute__((weak));
int DuoAudioReadRegisters(UINT32 Out[8]) __attribute__((weak));
int DuoAudioFifoTest(UINT32 Out[3]) __attribute__((weak));
int DuoAudioPlayStereoTone(UINT32 FrequencyHz, UINT32 Milliseconds,
                           INT16 LeftAmplitude,
                           INT16 RightAmplitude) __attribute__((weak));

static int DuoAudioBoardPresent(void) {
    return DuoAudioInitialize != 0 && DuoAudioReadRegisters != 0 &&
           DuoAudioFifoTest != 0 && DuoAudioPlayStereoTone != 0;
}

void CommandDuoAudio(int Argc, char **Argv) {
    const char *Sub = (Argc > 1) ? Argv[1] : "regs";

    if (!DuoAudioBoardPresent()) {
        (void)Sub;
        gConsole.Write("duoaudio: only on milk-v-duo-s\n");
        return;
    }

    if (Sub[0] == 'b') {
        gConsole.Write("duoaudio: both 1.5s\n");
        (void)DuoAudioPlayStereoTone(1000u, 1500u,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE);
        return;
    }
    if (Sub[0] == 'L') {
        gConsole.Write("duoaudio: left-only 1.5s\n");
        (void)DuoAudioPlayStereoTone(1000u, 1500u,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE, 0);
        return;
    }
    if (Sub[0] == 'R') {
        gConsole.Write("duoaudio: right-only 1.5s\n");
        (void)DuoAudioPlayStereoTone(1000u, 1500u, 0,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE);
        return;
    }
    if (Sub[0] == 'l') {
        gConsole.Write("duoaudio: lr 1/3 L only\n");
        (void)DuoAudioPlayStereoTone(1000u, 600u,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE, 0);
        HalDelayMilliseconds(150u);
        gConsole.Write("duoaudio: lr 2/3 R only\n");
        (void)DuoAudioPlayStereoTone(1000u, 600u, 0,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE);
        HalDelayMilliseconds(150u);
        gConsole.Write("duoaudio: lr 3/3 both\n");
        (void)DuoAudioPlayStereoTone(1000u, 600u,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE,
                                     (INT16)DUO_AUDIO_SHELL_AMPLITUDE);
        gConsole.Write("duoaudio: lr done\n");
        return;
    }
    if (Sub[0] == 'f') {
        UINT32 Fifo[3] = {0u, 0u, 0u};
        int Result = DuoAudioFifoTest(Fifo);

        gConsole.Write("duoaudio: fifo before=");
        gConsole.WriteHex32(Fifo[0]);
        gConsole.Write(" after=");
        gConsole.WriteHex32(Fifo[1]);
        gConsole.Write(" again=");
        gConsole.WriteHex32(Fifo[2]);
        gConsole.Write(Result != 0 ? " (status changed → FIFO alive)\n"
                                   : " (status unchanged → writes not consumed?)\n");
        return;
    }
    {
        UINT32 Registers[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

        (void)DuoAudioInitialize();
        if (DuoAudioReadRegisters(Registers) != 0) {
            gConsole.Write("duoaudio: read fail\n");
            return;
        }
        gConsole.Write("duoaudio: en1=");
        gConsole.WriteHex32(Registers[0]);
        gConsole.Write(" blk=");
        gConsole.WriteHex32(Registers[1]);
        gConsole.Write(" frame=");
        gConsole.WriteHex32(Registers[2]);
        gConsole.Write(" slot1=");
        gConsole.WriteHex32(Registers[3]);
        gConsole.Write("\n");
        gConsole.Write("duoaudio: clk1=");
        gConsole.WriteHex32(Registers[4]);
        gConsole.Write(" enable=");
        gConsole.WriteHex32(Registers[6]);
        gConsole.Write(" dac_ctrl0=");
        gConsole.WriteHex32(Registers[7]);
        gConsole.Write("\n");
    }
}

void ShellCommandsDuoAudioRegister(void) {
    gConsole.Register("duoaudio",
                      "duoaudio [regs|fifo|lr|L|R|b] — 取证与左右测试",
                      CommandDuoAudio);
}
