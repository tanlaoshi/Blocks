/*
 * I2sTransmit.c — Duo S 的 I2S3 主机 TX（送样点到片内 DAC）
 *
 * 对表厂商：
 *   machine dai_fmt = I2S | IB_IF | CBS_CFS（cv181x_cv181xdac.c）
 *   经典 I2S 立体声：FRAME(32)|FS_ACT(16) + FS_OFFSET_1 + FS_ACT_HIGH
 *   SLOT_NUM(2) | SLOT_SETTING2=0x03 | SLOT/DATA_SIZE(16)
 *   DMA：只打 TX_WR_PORT_CH0，addr_width=4 ⇒ **一拍 32bit = L|R 打包**
 *        （真机：两次 16bit 写把 L/R 都挤到右耳；禁止再双写）
 *
 * 别改：先停 I2S → 复位 FIFO/I2S → 写配置 → 开时钟/使能。
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
    /* 帧 32 bit ⇒ BCLK = rate * 32；48 kHz → div=16 */
    UINT32 BclkDiv = 24576000u / (DUO_AUDIO_RATE_HZ * 32u);

    DuoI2sClockGateEnable();
    *DuoI2sRegister(DUO_I2S_ENABLE) = 0u;
    *DuoI2sRegister(DUO_I2S_RESET) = DUO_I2S_RESET_TX_PULSE;
    *DuoI2sRegister(DUO_I2S_RESET) = 0u;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = DUO_I2S_TX_FIFO_RESET;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = 0u;

    *DuoI2sRegister(DUO_I2S_BLK_MODE) =
        DUO_I2S_TX_MODE | DUO_I2S_MASTER_MODE |
        DUO_I2S_RX_SAMPLE_EDGE_POSITIVE |
        DUO_I2S_TX_SAMPLE_EDGE_NEGATIVE |
        DUO_I2S_FS_SAMPLE_EDGE_NEGATIVE;

    *DuoI2sRegister(DUO_I2S_FRAME) =
        DUO_I2S_FRAME_LEN(32u) | DUO_I2S_FS_ACT(16u) |
        DUO_I2S_FS_ACT_HIGH | DUO_I2S_FS_OFFSET_1_BIT |
        DUO_I2S_FS_IDEF_FRAME_SYNC;

    *DuoI2sRegister(DUO_I2S_SLOT1) = DUO_I2S_SLOT_NUM(DUO_AUDIO_CHANNELS) |
                                     DUO_I2S_SLOT_SIZE(DUO_AUDIO_BITS) |
                                     DUO_I2S_DATA_SIZE(DUO_AUDIO_BITS);
    *DuoI2sRegister(DUO_I2S_SLOT2) = DUO_I2S_SLOT2_STEREO;
    *DuoI2sRegister(DUO_I2S_DATA_FORMAT) = DUO_I2S_WORD_LEN_16;
    *DuoI2sRegister(DUO_I2S_CLK_CTRL1) = DUO_I2S_MCLK_DIV(1u) | DUO_I2S_BCLK_DIV(BclkDiv);
    *DuoI2sRegister(DUO_I2S_CLK_CTRL0) |= DUO_I2S_AUD_ENABLE;
    *DuoI2sRegister(DUO_I2S_INT_EN) = 0u;
    *DuoI2sRegister(DUO_I2S_ENABLE) = DUO_I2S_ON;
    return 0;
}

/*
 * 一帧 = 一次 32bit 写（与 DMA addr_width=4 同形）。
 * TX_STATUS 低 5 位不是空位；上层按每毫秒 32 帧节流。
 */
int DuoI2sWriteFrame(INT16 Left, INT16 Right) {
    UINT32 Packed = (UINT32)(UINT16)Left |
                    (((UINT32)(UINT16)Right) << 16);

    *DuoI2sRegister(DUO_I2S_TX_WR_CH0) = Packed;
    return 0;
}

void DuoI2sDisable(void) {
    *DuoI2sRegister(DUO_I2S_ENABLE) = 0u;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = DUO_I2S_TX_FIFO_RESET;
    *DuoI2sRegister(DUO_I2S_FIFO_RESET) = 0u;
}
