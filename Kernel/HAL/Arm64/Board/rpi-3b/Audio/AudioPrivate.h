/*
 * AudioPrivate.h — Pi 3B 耳机孔（AV 孔）PWM 音频（audio-pi-1）
 *
 * 口径（Docs/29 §6.1 已钉死，评审 ✅）：
 *   PWM  CPU 窗 0x3F20C000（上游 bcm283x.dtsi pwm@7e20c000，窗 0x28）
 *   GPIO40/41 都 ALT0（&pwm { pinctrl-0 = <&pwm0_gpio40 &pwm1_gpio41>; }）
 *   时钟 CM_PWMCTL 0x3F1010A0 / CM_PWMDIV 0x3F1010A4，目标 **10 MHz**（评审纠：勿用 50）
 *
 * 本刀只做"耳机孔能听见"：固定 1 kHz 方波蜂鸣（PWM 占空比开关），
 * **不上 DMA、不做 PCM**（`HalAudioPlayPcm(Samples!=NULL)` 如实返回 0）。
 * 禁：GPIO14/15（壳 mini-UART）、GPIO32/33（BT PL011）、WiFi SDIO 脚。
 */
#ifndef RPI_AUDIO_PRIVATE_H
#define RPI_AUDIO_PRIVATE_H

#include "BootTypes.h"

#define PI_PWM_BASE          0x3F20C000ULL
#define PI_PWM_CTL           0x00u
#define PI_PWM_STA           0x04u
#define PI_PWM_RNG1          0x10u
#define PI_PWM_DAT1          0x14u
#define PI_PWM_RNG2          0x20u
#define PI_PWM_DAT2          0x24u

#define PI_PWM_CTL_PWEN1     (1u << 0)
#define PI_PWM_CTL_PWEN2     (1u << 8)
/* 评审 09:00：MSEN 是 Mark-Space enable（不是"串行器"）；FIFO 串行需 MODE/USEF */
#define PI_PWM_CTL_MSEN1     (1u << 7)
#define PI_PWM_CTL_MSEN2     (1u << 15)
#define PI_PWM_FIF1          0x18u

#define PI_CM_BASE           0x3F101000ULL
#define PI_CM_PWMCTL         0xA0u
#define PI_CM_PWMDIV         0xA4u
#define PI_CM_PASSWORD       0x5A000000u
#define PI_CM_ENABLE         0x10u
#define PI_CM_BUSY           0x80u
#define PI_CM_SRC_OSC        1u      /* 19.2 MHz 晶振 */
/* 19.2 MHz / 10 MHz = 1.92 → DIVI=1, DIVF=0.92*4096≈3768 */
#define PI_CM_PWM_DIVI       1u
#define PI_CM_PWM_DIVF       3768u

#define PI_AUDIO_PWM_HZ      10000000u  /* 评审口径：10 MHz */
#define PI_AUDIO_BEEP_HZ     1000u      /* 1 kHz 方波 */
#define PI_AUDIO_BEEP_MS     150u
#define PI_AUDIO_GAP_MS      80u
#define PI_AUDIO_BEEPS       2u

int PiPwmClockEnable(void);
int PiPwmTransportInitialize(void);
int PiPwmSetTone(UINT32 FrequencyHz, UINT32 DutyPercent);
void PiPwmSilence(void);

/* MSEN（serialiser/MASH）模式：评审要求补的对照实验（非默认路径） */
int PiPwmSerialiserEnable(UINT32 Period);
void PiPwmSerialiserPush(UINT32 Value);
void PiPwmSerialiserDisable(void);

int BoardAudioInitialize(void);
int BoardAudioProbe(void);
int BoardAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                      UINT32 Channels, UINT32 Bits);
void BoardAudioStop(void);
void BoardAudioBeep(void);
/* 取证（audioreg 命令用，只读） */
int BoardAudioReadRegisters(UINT32 Out[8]);

#endif
