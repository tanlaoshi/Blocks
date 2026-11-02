/*
 * DuoAudio.c — `duoaudio regs|fifo`：Duo 音频寄存器取证（只读 + 8 帧写测试）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"

#if defined(__riscv)
#include "../../HAL/RiscV/Board/milk-v-duo-s/Audio/AudioPrivate.h"
#endif

void CommandDuoAudio(int Argc, char **Argv) {
#if !defined(__riscv)
    (void)Argc;
    (void)Argv;
    gConsole.Write("duoaudio: only on milk-v-duo-s\n");
#else
    const char *Sub = (Argc > 1) ? Argv[1] : "regs";

    if (Sub[0] == 'f') {
        UINT32 Fifo[3] = {0u, 0u, 0u};
        int Rc = DuoAudioFifoTest(Fifo);

        gConsole.Write("duoaudio: fifo before=");
        gConsole.WriteHex32(Fifo[0]);
        gConsole.Write(" after=");
        gConsole.WriteHex32(Fifo[1]);
        gConsole.Write(" again=");
        gConsole.WriteHex32(Fifo[2]);
        gConsole.Write(Rc ? " (status changed → FIFO alive)\n" : " (status unchanged → writes not consumed?)\n");
        return;
    }
    {
        UINT32 Regs[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

        (void)DuoAudioInitialize();
        if (DuoAudioReadRegisters(Regs) != 0) {
            gConsole.Write("duoaudio: read fail\n");
            return;
        }
        gConsole.Write("duoaudio: en1=");
        gConsole.WriteHex32(Regs[0]);
        gConsole.Write(" blk=");
        gConsole.WriteHex32(Regs[1]);
        gConsole.Write(" frame=");
        gConsole.WriteHex32(Regs[2]);
        gConsole.Write(" slot1=");
        gConsole.WriteHex32(Regs[3]);
        gConsole.Write("\n");
        gConsole.Write("duoaudio: clk1=");
        gConsole.WriteHex32(Regs[4]);
        gConsole.Write(" enable=");
        gConsole.WriteHex32(Regs[6]);
        gConsole.Write(" dac_ctrl0=");
        gConsole.WriteHex32(Regs[7]);
        gConsole.Write("\n");
    }
#endif
}

void ShellCommandsDuoAudioRegister(void) {
    gConsole.Register("duoaudio", "duoaudio [regs|fifo] — Duo I2S3/DAC 取证", CommandDuoAudio);
}
