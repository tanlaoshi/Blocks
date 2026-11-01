/*
 * WifiKeyDelete.c — MM_KEY_DEL_REQ(0x26) → CFM(0x27)
 *
 * Docs/21 §1e：两钥装完后删 PTK，测「曾装过 PTK 再删」是否让组播走 GTK
 * （异于 noptk=从未装 PTK → 明文）。
 */
#include "WifiKeyAddPrivate.h"
#include "WifiAssocPrivate.h"
#include "WifiEapolPrivate.h"
#include "CString.h"

#define AIC_KEY_DELETE_REQUEST_LENGTH 1u
#define AIC_PAIRWISE_HARDWARE_KEY_INDEX_NONE 0xFFFFFFFFu

/* PTK MM_KEY_ADD_CFM 的 hw_key_idx；未装过 = NONE */
UINT32 gAicPairwiseHardwareKeyIndex = AIC_PAIRWISE_HARDWARE_KEY_INDEX_NONE;
/* probe.cfg ptkdel=1 */
UINT32 gAicDeletePairwiseKeyAfterInstall;

void AicKeyRememberPairwiseHardwareIndex(UINT32 HardwareKeyIndex) {
    gAicPairwiseHardwareKeyIndex = HardwareKeyIndex;
}

void AicKeyClearPairwiseHardwareIndex(void) {
    gAicPairwiseHardwareKeyIndex = AIC_PAIRWISE_HARDWARE_KEY_INDEX_NONE;
}

static int KeyDeleteCfmDone(void) {
    return (int)gAicEapolKeyDeleteCfm;
}

static int KeyDeleteWaitCfm(SDHCI_HOST *Host) {
    AIC_EAPOL_COUNTERS Counters;

    gAicEapolKeyDeleteCfm = 0u;
    memset(&Counters, 0, sizeof(Counters));
    if (AicEapolPollRun(Host, AIC_HANDSHAKE_KEY_ADD_MS, &Counters, KeyDeleteCfmDone) != 0) {
        SdhciMark("WiFi: key delete: SDIO read failed while waiting for MM_KEY_DEL_CFM(0x27)\n");
        return -1;
    }
    if (gAicEapolKeyDeleteCfm == 0u) {
        SdhciMark("WiFi: key delete: no MM_KEY_DEL_CFM(0x27) in ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_KEY_ADD_MS);
        SdhciMark("ms\n");
        return -1;
    }
    return 0;
}

int AicKeyDeleteByHardwareIndex(SDHCI_HOST *Host, UINT32 HardwareKeyIndex) {
    UINT8 Request[AIC_KEY_DELETE_REQUEST_LENGTH];

    if (HardwareKeyIndex > 0xFFu) {
        SdhciMark("WiFi: key delete: bad hw_key_idx=");
        SdhciMarkDecimal("", HardwareKeyIndex);
        SdhciMark("\n");
        return -1;
    }
    Request[0] = (UINT8)HardwareKeyIndex;
    SdhciMark("WiFi: key delete: hw_key_idx=");
    SdhciMarkDecimal("", HardwareKeyIndex);
    SdhciMark("\n");
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_KEY_DELETE_REQ, (UINT16)AIC_TASK_MM, Request,
                         (UINT16)AIC_KEY_DELETE_REQUEST_LENGTH) != 0) {
        return -1;
    }
    if (KeyDeleteWaitCfm(Host) != 0) {
        return -1;
    }
    SdhciMark("WiFi: key delete: cfm ok hw_key_idx=");
    SdhciMarkDecimal("", HardwareKeyIndex);
    SdhciMark("\n");
    return 0;
}

/*
 * probe ptkdel=1：用已记住的 PTK hw_key_idx 发 MM_KEY_DEL。
 * 0 = 未开探针或删成功；-1 = 无索引 / 固件未确认。
 */
int AicKeyDeletePairwiseIfProbe(SDHCI_HOST *Host) {
    if (gAicDeletePairwiseKeyAfterInstall == 0u) {
        return 0;
    }
    if (gAicSkipPairwiseKeyInstall != 0u) {
        SdhciMark("WiFi: ptkdel skipped (noptk also set; Docs/21 §1e)\n");
        return 0;
    }
    if (gAicPairwiseHardwareKeyIndex == AIC_PAIRWISE_HARDWARE_KEY_INDEX_NONE) {
        SdhciMark("WiFi: ptkdel FAILED: no pairwise hw_key_idx (PTK was not installed)\n");
        return -1;
    }
    SdhciMark("WiFi: ptkdel after both keys (probe ptkdel=1; Docs/21 §1e)\n");
    if (AicKeyDeleteByHardwareIndex(Host, gAicPairwiseHardwareKeyIndex) != 0) {
        return -1;
    }
    AicKeyClearPairwiseHardwareIndex();
    return 0;
}
