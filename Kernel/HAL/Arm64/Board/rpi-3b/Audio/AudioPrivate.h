/*
 * AudioPrivate.h — Pi 3B 耳机孔（AV 孔）PWM 音频
 *
 * audio-pi-1（否证）：OSC→10 MHz + MS/DAT 方波 —— 寄存器齐、人耳无声。
 * audio-pi-1b：对表 Circle `pwmsoundbasedevice` —— **PLLD→250 MHz** + **USEF FIFO**
 *   （DMA 可后接；本刀 CPU 喂 FIFO 蜂鸣）。Linux dts `assigned-clock-rates=10M`
 *   是 pwm-bcm2835 驱动口径，**不是**裸机模拟孔载波。
 *
 * 禁：GPIO14/15（壳）、GPIO32/33（BT）、WiFi SDIO。
 */
#ifndef RPI_AUDIO_PRIVATE_H
#define RPI_AUDIO_PRIVATE_H

#include "BootTypes.h"

#define PI_PWM_BASE          0x3F20C000ULL
#define PI_PWM_CTL           0x00u
#define PI_PWM_STA           0x04u
#define PI_PWM_DMAC          0x08u
#define PI_PWM_RNG1          0x10u
#define PI_PWM_DAT1          0x14u
#define PI_PWM_FIF1          0x18u
#define PI_PWM_RNG2          0x20u
#define PI_PWM_DAT2          0x24u

#define PI_PWM_CTL_PWEN1     (1u << 0)
#define PI_PWM_CTL_MODE1     (1u << 1)
#define PI_PWM_CTL_USEF1     (1u << 5)
#define PI_PWM_CTL_CLRF1     (1u << 6)
#define PI_PWM_CTL_MSEN1     (1u << 7)
#define PI_PWM_CTL_PWEN2     (1u << 8)
#define PI_PWM_CTL_MODE2     (1u << 9)
#define PI_PWM_CTL_USEF2     (1u << 13)
#define PI_PWM_CTL_MSEN2     (1u << 15)

#define PI_PWM_STA_FULL1     (1u << 0)
#define PI_PWM_STA_EMPT1     (1u << 1)

#define PI_CM_BASE           0x3F101000ULL
#define PI_CM_PWMCTL         0xA0u
#define PI_CM_PWMDIV         0xA4u
#define PI_CM_PASSWORD       0x5A000000u
#define PI_CM_ENABLE         0x10u
#define PI_CM_BUSY           0x80u
#define PI_CM_SRC_OSC        1u
#define PI_CM_SRC_PLLD       6u /* Circle：PLLD≈500 MHz */

/* Circle RASPPI≤3：PWM 音频载波 250 MHz（PLLD/2） */
#define PI_AUDIO_PWM_HZ      250000000u
#define PI_CM_PWM_DIVI       2u
#define PI_CM_PWM_DIVF       0u

#define PI_AUDIO_SAMPLE_HZ   48000u
#define PI_AUDIO_BEEP_HZ     1000u
#define PI_AUDIO_BEEP_MS     150u
#define PI_AUDIO_GAP_MS      80u
#define PI_AUDIO_BEEPS       2u

int PiPwmClockEnable(void);
int PiPwmTransportInitialize(void);
int PiPwmSetTone(UINT32 FrequencyHz, UINT32 DutyPercent);
void PiPwmSilence(void);

/* audio-pi-1b：USEF FIFO 蜂鸣（Circle 同形；CPU 喂，不上 DMA） */
int PiPwmFifoTone(UINT32 FrequencyHz, UINT32 Milliseconds);

/* 探针对照（audiomsen）：MSEN 位；勿与 USEF 混淆 */
int PiPwmSerialiserEnable(UINT32 Period);
void PiPwmSerialiserPush(UINT32 Value);
void PiPwmSerialiserDisable(void);

int BoardAudioInitialize(void);
int BoardAudioProbe(void);
int BoardAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                      UINT32 Channels, UINT32 Bits);
void BoardAudioStop(void);
void BoardAudioBeep(void);
int BoardAudioReadRegisters(UINT32 Out[8]);

#endif
