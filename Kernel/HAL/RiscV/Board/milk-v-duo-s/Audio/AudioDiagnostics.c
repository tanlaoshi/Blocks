/*
 * AudioDiagnostics.c — Duo 音频寄存器取证 + FIFO 写测试（audio-duo-1 排障）
 *
 * 为什么要它：`play` 打了 `play: beep`、Probe（DAC CTRL0 bit0）也过，但耳机没声。
 * 必须区分：① 我们写的 I2S 配置有没有落进去；② TX FIFO 写有没有被吃掉（TX_STATUS 变化）；
 *           ③ DAC 的模拟侧寄存器（AFE1 音量 / ANA2）是不是静音态。
 *
 * 只读 + 一次 8 帧写测试，不改音频配置。
 */
#include "AudioPrivate.h"

#define DUO_I2S_CLK_CTRL0_    0x60u
#define DUO_I2S_TX_STATUS_    0x48u

int DuoAudioReadRegisters(UINT32 Out[8]) {
    volatile UINT32 *I2s = (volatile UINT32 *)(UINTN)DUO_I2S3_BASE;
    volatile UINT32 *Dac = (volatile UINT32 *)(UINTN)DUO_DAC_BASE;
    volatile UINT32 *En1 = (volatile UINT32 *)(UINTN)(DUO_CLK_BASE + (UINT64)DUO_CLK_EN_1);

    if (Out == 0) {
        return -1;
    }
    Out[0] = *En1;                                   /* 时钟门控（I2S3 bit27 应为 1） */
    Out[1] = *(volatile UINT32 *)(UINTN)(DUO_I2S3_BASE + DUO_I2S_BLK_MODE);
    Out[2] = *(volatile UINT32 *)(UINTN)(DUO_I2S3_BASE + DUO_I2S_FRAME);
    Out[3] = *(volatile UINT32 *)(UINTN)(DUO_I2S3_BASE + DUO_I2S_SLOT1);
    Out[4] = *(volatile UINT32 *)(UINTN)(DUO_I2S3_BASE + DUO_I2S_CLK_CTRL1);
    Out[5] = *I2s;                                   /* 占位：读基址（便于对照） */
    Out[6] = *(volatile UINT32 *)(UINTN)(DUO_I2S3_BASE + DUO_I2S_ENABLE);
    Out[7] = *Dac;                                   /* DAC CTRL0 */
    return 0;
}

/* 写 8 帧，报告 TX_STATUS 前后变化：能变 → FIFO 通路活着；不变 → 写没被吃 */
int DuoAudioFifoTest(UINT32 Out[3]) {
    volatile UINT32 *Status = (volatile UINT32 *)(UINTN)(DUO_I2S3_BASE + DUO_I2S_TX_STATUS_);
    UINT32 Before = *Status;
    UINT32 Index;
    UINT32 After;

    for (Index = 0u; Index < 8u; Index++) {
        (void)DuoI2sWriteFrame((INT16)1000, (INT16)-1000);
    }
    After = *Status;
    if (Out != 0) {
        Out[0] = Before;
        Out[1] = After;
        Out[2] = *Status; /* 再读一次，避免只看一拍 */
    }
    return (Before != After) ? 1 : 0;
}
