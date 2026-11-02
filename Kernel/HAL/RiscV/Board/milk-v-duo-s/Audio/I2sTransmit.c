/*
 * I2sTransmit.c — Duo S 的 I2S3 主机 TX（送样点到片内 DAC）
 *
 * 对表厂商 sound/soc/cvitek/cv1835_i2s.c 的 hw_params（16-bit / 2ch / 主模式）：
 *   BLK_MODE_SETTING = TX_MODE | MASTER_MODE
 *   FRAME_SETTING    = FRAME_LENGTH(16) | FS_ACT_LENGTH(16)
 *   SLOT_SETTING1    = SLOT_SIZE(16) | DATA_SIZE(16)
 *   DATA_FORMAT      = WORD_LEN_16
 *   I2S_CLK_CTRL1    = BCLK_DIV(24.576MHz / (48000*32)) = 16
 *   I2S_CLK_CTRL0   |= AUD_ENABLE(bit8)
 * 时钟门控：clk-cv181x.c:87-88 → 0x03002000 + 0x004 bit27。
 *
 * 别改：先停 I2S → 复位 FIFO/I2S → 写配置 → 开时钟/使能；顺序反了会送不出字。
 */
#include "AudioPrivate.h"
#include "Hal.h"

static volatile UINT32 *DuoI2sRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DUO_I2S3_BASE + (UINT64)Offset);
}

static void DuoI2sClockGateEnable(void) {
    volatile UINT32 *Enable1 = (volatile UINT32 *)(UINTN)(DUO_CLK_BASE + (UINT64)DUO_CLK_EN_1);
    UINT32 Value = *Enable1;

    Value |= (1u << DUO_CLK_I2S3_BIT);
    *Enable1 = Value;
}

int DuoI2sInitialize(void) {
    UINT32 BclkDiv = 24576000u / (DUO_AUDIO_RATE_HZ * 32u); /* = 16 */

    DuoI2sClockGateEnable();
    *DuoI2sRegister(DUO_I2S_ENABLE) = 0u;
    *DuoI2sRegister(DUO_I2S_RESET) = DUO_I2S_RESET_TX_PULSE;
    *DuoI2sRegister(DUO_I2S_RESET) = 0u;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = DUO_I2S_TX_FIFO_RESET;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = 0u;

    *DuoI2sRegister(DUO_I2S_BLK_MODE) = DUO_I2S_TX_MODE | DUO_I2S_MASTER_MODE;
    *DuoI2sRegister(DUO_I2S_FRAME) = DUO_I2S_FRAME_LEN(DUO_AUDIO_BITS) |
                                     DUO_I2S_FS_ACT(DUO_AUDIO_BITS);
    *DuoI2sRegister(DUO_I2S_SLOT1) = DUO_I2S_SLOT_SIZE(DUO_AUDIO_BITS) |
                                     DUO_I2S_DATA_SIZE(DUO_AUDIO_BITS);
    *DuoI2sRegister(DUO_I2S_DATA_FORMAT) = DUO_I2S_WORD_LEN_16;
    *DuoI2sRegister(DUO_I2S_CLK_CTRL1) = DUO_I2S_MCLK_DIV(1u) | DUO_I2S_BCLK_DIV(BclkDiv);
    *DuoI2sRegister(DUO_I2S_CLK_CTRL0) |= DUO_I2S_AUD_ENABLE;
    *DuoI2sRegister(DUO_I2S_INT_EN) = 0u;
    *DuoI2sRegister(DUO_I2S_ENABLE) = DUO_I2S_ON;
    return 0;
}

/*
 * 写一帧（左/右各 16 bit），直接写、不轮询 TX_STATUS。
 *
 * 教训（2026-11-02 10:10 真机）：TX_STATUS(0x48) 只有 bit23 有文档定义（RESET_TX_SCLK），
 * **低 5 位不是"FIFO 剩余空位"**；按那个假设写，条件恒不成立 → 一帧都没写进去（duoaudio fifo
 * 读到 0x60000000 前后不变）。改为由上层按"每毫秒 32 帧"节流（FIFO 深 32，1 ms 正好取空）。
 */
int DuoI2sWriteFrame(INT16 Left, INT16 Right) {
    *DuoI2sRegister(DUO_I2S_TX_WR_CH0) = (UINT32)(UINT16)Left;
    *DuoI2sRegister(DUO_I2S_TX_WR_CH1) = (UINT32)(UINT16)Right;
    return 0;
}

void DuoI2sDisable(void) {
    *DuoI2sRegister(DUO_I2S_ENABLE) = 0u;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = DUO_I2S_TX_FIFO_RESET;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = 0u;
}
