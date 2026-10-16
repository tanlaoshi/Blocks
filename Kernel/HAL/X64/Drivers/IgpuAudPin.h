/*
 * IgpuAudPin.h — HDA HDMI 用的核显窄口（不拉 Igpu 总头）
 */
#ifndef IGPU_AUD_PIN_H
#define IGPU_AUD_PIN_H

#include "BootTypes.h"

int IgpuAudPinOk(void);
int IgpuAudForcewake(void);
UINT32 IgpuAudRead32(UINT32 Off);
void IgpuAudWrite32(UINT32 Off, UINT32 Val);

#endif
