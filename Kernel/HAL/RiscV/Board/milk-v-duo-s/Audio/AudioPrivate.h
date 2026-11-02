/*
 * AudioPrivate.h — Duo S → PoE HAT 耳机孔（I2S3 → 片内 DAC），audio-duo-1 / 1b
 *
 * 物理：本板无 3.5mm；孔在 PoE HAT（排针 AUDIO_OUT_L/R → HAT 座）。手测须插 HAT。
 *
 * 出处（全部来自厂商树，勿猜）：
 *   dts  ：i2s3@04130000；dac@0300A000；audio_clock 24576000；card_name=cv182xa_dac
 *   寄存器：sound/soc/cvitek/local.h、codecs/cv181xadac.h
 *   machine：cv181x_cv181xdac.c dai_fmt = I2S|IB_IF|CBS_CFS
 *   DMA   ：TX_WR_PORT_CH0，addr_width=4 ⇒ 一拍写 32bit（L 低 16 | R 高 16）
 *   时钟门控：clk-cv181x.c；REG_CLK_EN_1 bit27 = I2S3，基址 0x03002000
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
#define DUO_I2S_SLOT2         0x0Cu
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
/* set_fmt(IB_IF)：RX 升沿 / TX 降沿 / FS 降沿 */
#define DUO_I2S_RX_SAMPLE_EDGE_POSITIVE (1u << 2)
#define DUO_I2S_TX_SAMPLE_EDGE_NEGATIVE (0u << 3)
#define DUO_I2S_FS_SAMPLE_EDGE_NEGATIVE (0u << 4)
#define DUO_I2S_FS_ACT_HIGH             (1u << 12)
#define DUO_I2S_FS_OFFSET_1_BIT         (1u << 13)
#define DUO_I2S_FS_IDEF_FRAME_SYNC      (0u << 14)
#define DUO_I2S_AUD_ENABLE    (1u << 8)
#define DUO_I2S_ON            1u
#define DUO_I2S_RESET_TX_PULSE 2u
#define DUO_I2S_TX_FIFO_RESET  0x00010000u
#define DUO_I2S_WORD_LEN_16   (1u << 1)
#define DUO_I2S_FRAME_LEN(l)  ((((UINT32)(l)) - 1u) & 0x1FFu)
#define DUO_I2S_FS_ACT(l)     (((((UINT32)(l)) - 1u) & 0xFFu) << 16)
#define DUO_I2S_SLOT_NUM(l)   ((((UINT32)(l)) - 1u) & 0xFu)
#define DUO_I2S_SLOT_SIZE(l)  (((((UINT32)(l)) - 1u) & 0xFFu) << 8)
#define DUO_I2S_DATA_SIZE(l)  (((((UINT32)(l)) - 1u) & 0x3FFu) << 16)
#define DUO_I2S_SLOT2_STEREO  0x03u
#define DUO_I2S_BCLK_DIV(d)   ((((UINT32)(d)) & 0xFFFFu) << 16)
#define DUO_I2S_MCLK_DIV(d)   (((UINT32)(d)) & 0xFFFFu)

/* DAC（cv181xadac.h） */
#define DUO_DAC_BASE          0x0300A000ULL
#define DUO_DAC_CTRL0         0x00u
#define DUO_DAC_CTRL1         0x04u
#define DUO_DAC_AFE0          0x0Cu
#define DUO_DAC_AFE1          0x10u
#define DUO_DAC_ANA2          0x28u
#define DUO_DAC_TXDAC_EN      (1u << 0)
#define DUO_DAC_I2S_RX_EN     (1u << 1)
#define DUO_DAC_CIC_MASK      0x3u
#define DUO_DAC_CIC_DS_64     0x0u /* 48 kHz：清 CIC 低 2 位 */
#define DUO_DAC_AFE0_TICK_48K 0x19u
#define DUO_DAC_AFE0_TICK_MASK 0x3Fu /* INIT_DLY_CNT */
#define DUO_DAC_ANALOG_LEFT_OVERRIDE_ENABLE  (1u << 16) /* 1 = mute */
#define DUO_DAC_ANALOG_RIGHT_OVERRIDE_ENABLE (1u << 17)
#define DUO_DAC_VOLUME_BOTH   0x00BF00BFu

#define DUO_AUDIO_RATE_HZ     48000u
#define DUO_AUDIO_BITS        16u
#define DUO_AUDIO_CHANNELS    2u
#define DUO_AUDIO_BEEP_HZ     1000u
#define DUO_AUDIO_BEEP_MS     150u
#define DUO_AUDIO_GAP_MS      80u
#define DUO_AUDIO_BEEPS       2u
#define DUO_AUDIO_AMPLITUDE   8000

int DuoI2sInitialize(void);
int DuoI2sWriteFrame(INT16 Left, INT16 Right);
void DuoI2sDisable(void);

int DuoDacInitialize(void);
int DuoDacProbe(void);

int DuoAudioInitialize(void);
int DuoAudioProbe(void);
int DuoAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits);
int DuoAudioPlayStereoTone(UINT32 FrequencyHz, UINT32 Milliseconds,
                           INT16 LeftAmplitude, INT16 RightAmplitude);
void DuoAudioStop(void);
void DuoAudioBeep(void);

int DuoAudioReadRegisters(UINT32 Out[8]);
int DuoAudioFifoTest(UINT32 Out[3]);

#endif
