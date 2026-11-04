/*
 * WifiKeyAddPrivate.h — PTK/GTK 装钥共用交换层（WifiKeyAdd.c / WifiKeyAddGroup.c）
 */
#ifndef WIFI_KEY_ADD_PRIVATE_H
#define WIFI_KEY_ADD_PRIVATE_H

#include "Wifi/HandshakePrivate.h"

#define AIC_KEY_ADD_SENT_OK 0u
#define AIC_KEY_ADD_SENT_FAILED 1u
#define AIC_KEY_ADD_CFM_NONE 0xFFFFFFFFu

typedef struct {
    UINT32 Sent;
    UINT32 CfmStatus;
    UINT32 HwIndex;
} AIC_KEY_ADD_RESULT;

AIC_KEY_ADD_RESULT AicKeyAddExchange(SDHCI_HOST *Host, UINT8 VifIndex, UINT8 StaIndex,
                                     UINT8 Pairwise, const UINT8 *Key, UINT8 KeyLength,
                                     UINT8 KeyIndex, UINT8 CipherSuite);
void AicKeyAddMarkResult(const AIC_KEY_ADD_RESULT *Result);
int AicKeyAddIsInstalled(const AIC_KEY_ADD_RESULT *Result);

#endif
