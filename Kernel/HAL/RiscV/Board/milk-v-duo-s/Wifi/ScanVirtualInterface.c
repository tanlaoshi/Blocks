/*
 * ScanVirtualInterface.c — ADD_IF / vif 缓存 + 扫描共用缓冲与 WaitConfirm
 *
 * 自 Scan.c 拆出（行数债）；行为不变。
 */
#include "Wifi/ScanPrivate.h"

static UINT32 sAicScanBuffer[(AIC_SCANU_START_LENGTH + 3u) / 4u];
static UINT32 sAicScanFrame[(AIC_SCAN_FRAME_MAX + 3u) / 4u];
static UINT8 sAicScanVifIndex;
static int sAicScanVifValid;

UINT8 *ScanFrameBuffer(void) {
    return (UINT8 *)sAicScanFrame;
}

UINT32 *ScanRequestBuffer(void) {
    return sAicScanBuffer;
}

UINT32 ScanNowMilliseconds(void) {
    return (UINT32)(SdhciReadTime() / (BOARD_TIMEBASE_HZ / 1000u));
}

void ScanPrintTransmit(const char *Name, UINT32 Length, const char *Note) {
    SdhciMark("WiFi: ");
    SdhciMark(Name);
    SdhciMark(" tx len=");
    SdhciMarkDecimal("", Length);
    SdhciMark(Note);
    SdhciMark("\n");
}

void ScanDumpBytes(const UINT8 *Data, UINT32 Length) {
    UINT32 Count = Length > AIC_BRINGUP_DUMP_BYTES ? AIC_BRINGUP_DUMP_BYTES : Length;
    UINT32 Index;

    SdhciMark("WiFi:  dump[");
    SdhciMarkDecimal("", Count);
    SdhciMark("] of ");
    SdhciMarkDecimal("", Length);
    for (Index = 0; Index < Count; Index++) {
        SdhciMarkHex(" ", (UINT32)Data[Index], 2);
    }
    SdhciMark("\n");
}

int ScanWaitConfirm(SDHCI_HOST *Host, UINT16 WantId, UINT16 AlternateId, UINT16 *OutId,
                    UINT32 *OutParamLength) {
    UINT32 Try;

    for (Try = 0; Try < AIC_SCAN_CFM_TRIES; Try++) {
        UINT16 Id = 0;
        UINT32 ParamLength = 0;

        if (AicReceiveFrameTimeout(Host, (UINT8 *)sAicScanFrame, AIC_SCAN_FRAME_MAX, &Id, 0,
                                   &ParamLength, 1u, AIC_CFM_TIMEOUT_MS) != 0) {
            continue;
        }
        if (Id == WantId || Id == AlternateId) {
            *OutId = Id;
            *OutParamLength = ParamLength;
            return 0;
        }
        SdhciMarkHex("WiFi: scan: ignore rx id=", Id, 4);
        SdhciMarkHex(" plen=", ParamLength, 4);
        SdhciMark("\n");
    }
    return -1;
}

static int ScanAddInterface(SDHCI_HOST *Host, UINT8 *OutVifIndex) {
    UINT32 Length = AicMmAddIfBuild((UINT8 *)sAicScanBuffer);
    UINT32 ParamLength = 0;
    UINT16 Id = 0;
    const AIC_ADD_IF_CFM *Confirm;

    ScanPrintTransmit("add if", Length, "");
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_ADD_IF_REQ, (UINT16)AIC_TASK_MM, sAicScanBuffer,
                         (UINT16)Length) != 0) {
        SdhciMark("WiFi: add if send FAILED (SDIO tx)\n");
        return -1;
    }
    if (ScanWaitConfirm(Host, (UINT16)AIC_MM_ADD_IF_CFM, (UINT16)AIC_SCAN_NO_ALTERNATE, &Id,
                        &ParamLength) != 0) {
        SdhciMark("WiFi: add if: no MM_ADD_IF_CFM(0x0007) in 2000ms\n");
        return -1;
    }
    if (ParamLength < AIC_ADD_IF_CFM_LENGTH) {
        SdhciMarkHex("WiFi: add if cfm too short plen=", ParamLength, 4);
        SdhciMark("\n");
        return -1;
    }
    Confirm = (const AIC_ADD_IF_CFM *)((const UINT8 *)sAicScanFrame + AIC_E2A_PARAM_OFF);
    if (Confirm->Status != 0u) {
        SdhciMarkHex("WiFi: add if cfm status=", Confirm->Status, 2);
        SdhciMark(" (firmware refused: no free VIF / bad MAC)\n");
        return -1;
    }
    *OutVifIndex = Confirm->InstanceNumber;
    SdhciMark("WiFi: add if cfm ok (vif=");
    SdhciMarkDecimal("", Confirm->InstanceNumber);
    SdhciMark(")\n");
    return 0;
}

int ScanVirtualInterfaceAcquire(SDHCI_HOST *Host, UINT8 *OutVifIndex) {
    if (sAicScanVifValid != 0) {
        *OutVifIndex = sAicScanVifIndex;
        SdhciMark("WiFi: add if reuse (vif=");
        SdhciMarkDecimal("", (UINT32)sAicScanVifIndex);
        SdhciMark(")\n");
        return 0;
    }
    if (ScanAddInterface(Host, OutVifIndex) != 0) {
        return -1;
    }
    sAicScanVifIndex = *OutVifIndex;
    sAicScanVifValid = 1;
    return 0;
}

int ScanVirtualInterfaceIsCached(void) {
    return sAicScanVifValid;
}

void ScanVirtualInterfaceInvalidate(void) {
    sAicScanVifValid = 0;
}

int AicScanVifCached(UINT8 *OutVifIndex) {
    if (sAicScanVifValid == 0) {
        return -1;
    }
    *OutVifIndex = sAicScanVifIndex;
    return 0;
}

int AicScanVifEnsure(SDHCI_HOST *Host, UINT8 *OutVifIndex) {
    if (sAicScanVifValid == 0) {
        SdhciMark("WiFi: join: no scan vif yet, adding one (MM_ADD_IF)\n");
    }
    return ScanVirtualInterfaceAcquire(Host, OutVifIndex);
}

void AicScanVifForget(void) {
    sAicScanVifValid = 0;
    sAicScanVifIndex = 0;
}
