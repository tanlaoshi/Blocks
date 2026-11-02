/*
 * AudioPrivate.h — Duo S → PoE HAT 耳机孔（I2S3 → 片内 DAC），audio-duo-1
 *
 * 物理：本板无 3.5mm；孔在 PoE HAT（排针 AUDIO_OUT_L/R → HAT 座）。手测须插 HAT。
 *
 * 出处（全部来自厂商树，勿猜）：
 *   dts  ：i2s3@04130000 `capability="tx"`，注释「I2S3 connect to internal DAC as TX」
 *          dac@0300A000 `compatible="cvitek,cv182xadac"`
 *          audio_clock fixed 24576000
 *   寄存器：sound/soc/cvitek/local.h（I2S 偏移）、sound/soc/codecs/cv181xadac.h（DAC 偏移）
 *   时钟门控：drivers/clk/cvitek/clk-cv181x.c；REG_CLK_EN_1 bit27 = I2S3，基址 0x03002000
 *
 * 本刀只做「HAT 孔能听见」：I2S3 TX + DAC + CPU 喂 FIFO 方波。
 * 不做 DMA、不做录音、不碰 I2S0（ADC）。
 */
#ifndef DUO_AUDIO_PRIVATE_H
#define DUO_AUDIO_PRIVATE_H

#include "BootTypes.h"

/* 时钟控制器 */
#define DUO_CLK_BASE          0x03002000ULL
#define DUO_CLK_EN_1          0x004u
#define DUO_CLK_I2S3_BIT      27u

/* I2S3（local.h） */
#define DUO_I2S3_BASE         0x04130000ULL
#define DUO_I2S_BLK_MODE      0x00u
#define DUO_I2S_FRAME         0x04u
#define DUO_I2S_SLOT1         0x08u
#define DUO_I2S_DATA_FORMAT   0x10u
#define DUO_I2S_ENABLE        0x18u
#define DUO_I2S_RESET         0x1Cu
#define DUO_I2S_INT_EN        0x20u
#define DUO_I2S_INT           0x24u
#define DUO_I2S_FIFO_RESET    0x30u
#define DUO_I2S_TX_STATUS     0x48u
#define DUO_I2S_CLK_CTRL0     0x60u
#define DUO_I2S_CLK_CTRL1     0x64u
#define DUO_I2S_TX_WR_CH0     0xC0u
#define DUO_I2S_TX_WR_CH1     0xC4u

#define DUO_I2S_TX_MODE       (1u << 0)
#define DUO_I2S_MASTER_MODE   (1u << 1)
#define DUO_I2S_AUD_ENABLE    (1u << 8)
#define DUO_I2S_ON            1u
#define DUO_I2S_RESET_TX_PULSE 2u
#define DUO_I2S_TX_FIFO_RESET  0x00010000u
#define DUO_I2S_WORD_LEN_16   (1u << 1)
#define DUO_I2S_FRAME_LEN(l)  ((((UINT32)(l)) - 1u) & 0x1FFu)
#define DUO_I2S_FS_ACT(l)     (((((UINT32)(l)) - 1u) & 0xFFu) << 16)
#define DUO_I2S_SLOT_SIZE(l)  (((((UINT32)(l)) - 1u) & 0xFFu) << 8)
#define DUO_I2S_DATA_SIZE(l)  (((((UINT32)(l)) - 1u) & 0x3FFu) << 16)
#define DUO_I2S_BCLK_DIV(d)   ((((UINT32)(d)) & 0xFFFFu) << 16)
#define DUO_I2S_MCLK_DIV(d)   (((UINT32)(d)) & 0xFFFFu)

/* DAC（cv181xadac.h） */
#define DUO_DAC_BASE          0x0300A000ULL
#define DUO_DAC_CTRL0         0x00u
#define DUO_DAC_AFE1          0x10u
#define DUO_DAC_TXDAC_EN      (1u << 0)
#define DUO_DAC_I2S_RX_EN     (1u << 1)

#define DUO_AUDIO_RATE_HZ     48000u
#define DUO_AUDIO_BITS        16u
#define DUO_AUDIO_CHANNELS    2u
#define DUO_AUDIO_BEEP_HZ     1000u
#define DUO_AUDIO_BEEP_MS     150u
#define DUO_AUDIO_GAP_MS      80u
#define DUO_AUDIO_BEEPS       2u
#define DUO_AUDIO_AMPLITUDE   8000    /* 16-bit 方波幅度，约 -12 dBFS */

int DuoI2sInitialize(void);
int DuoI2sWriteFrame(INT16 Left, INT16 Right);
void DuoI2sDisable(void);

int DuoDacInitialize(void);
int DuoDacProbe(void);

int DuoAudioInitialize(void);
int DuoAudioProbe(void);
int DuoAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits);
void DuoAudioStop(void);
void DuoAudioBeep(void);

#endif
