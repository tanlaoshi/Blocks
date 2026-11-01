/*
 * WifiDataPathWrite.c — 数据通道流控 + 对齐块写（从 WifiEapolTx.c 拆出，≤300）
 *
 * 厂商：aicwf_sdio 流控门槛与 4/TAIL/512 对齐算术；写 fn1 0x10 块模式。
 */
#include "WifiNetPrivate.h"

#define AIC_DATA_PATH_BLOCK 512u
#define AIC_DATA_PATH_ALIGN 4u
#define AIC_DATA_PATH_TAIL 4u
#define AIC_DATA_PATH_MAX 512u
#define AIC_DATA_PATH_FLOW_CONTROL_MINIMUM 3u
#define AIC_DATA_PATH_FLOW_CONTROL_RETRIES 50u
#define AIC_DATA_PATH_FLOW_CONTROL_FAST_TRIES 30u
#define AIC_DATA_PATH_FLOW_CONTROL_MID_TRIES 40u
#define AIC_DATA_PATH_FLOW_CONTROL_FAST_DELAY_MICROSECONDS 200u
#define AIC_DATA_PATH_FLOW_CONTROL_MID_DELAY_MILLISECONDS 2u
#define AIC_DATA_PATH_FLOW_CONTROL_LATE_DELAY_MILLISECONDS 10u

static int DataPathFlowControl(SDHCI_HOST *Host, UINT32 *OutCredit, const char *Tag) {
    UINT32 Try = 0;
    UINT8 Credit = 0;

    for (;;) {
        if (SdhciIoRead8(Host, 1u, AIC_FLOW_CTRL_REG_V3, &Credit) != 0) {
            AicMarkR5(Host->LastStatus);
            SdhciMark("WiFi: ");
            SdhciMark(Tag);
            SdhciMark(": fc read failed (cmd52 fn1 0x03)\n");
            return -1;
        }
        if ((UINT32)Credit >= AIC_DATA_PATH_FLOW_CONTROL_MINIMUM) {
            *OutCredit = (UINT32)Credit;
            return 0;
        }
        if (Try >= AIC_DATA_PATH_FLOW_CONTROL_RETRIES) {
            *OutCredit = (UINT32)Credit;
            return -1;
        }
        Try++;
        if (Try < AIC_DATA_PATH_FLOW_CONTROL_FAST_TRIES) {
            SdhciDelayMicroseconds(AIC_DATA_PATH_FLOW_CONTROL_FAST_DELAY_MICROSECONDS);
        } else if (Try < AIC_DATA_PATH_FLOW_CONTROL_MID_TRIES) {
            SdhciDelayMilliseconds(AIC_DATA_PATH_FLOW_CONTROL_MID_DELAY_MILLISECONDS);
        } else {
            SdhciDelayMilliseconds(AIC_DATA_PATH_FLOW_CONTROL_LATE_DELAY_MILLISECONDS);
        }
    }
}

int AicWifiDataPathWrite(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 Length, const char *Tag,
                         const char *FailText) {
    UINT32 Aligned = (Length + (AIC_DATA_PATH_ALIGN - 1u)) & ~(AIC_DATA_PATH_ALIGN - 1u);
    UINT32 Padded = Aligned;
    UINT32 Credit = 0;
    UINT32 Index;

    for (Index = Length; Index < Aligned; Index++) {
        Buffer[Index] = 0;
    }
    if ((Aligned % AIC_DATA_PATH_BLOCK) != 0u) {
        Padded = ((Aligned + AIC_DATA_PATH_TAIL + AIC_DATA_PATH_BLOCK - 1u) /
                  AIC_DATA_PATH_BLOCK) *
                 AIC_DATA_PATH_BLOCK;
    }
    if (Padded > AIC_DATA_PATH_MAX) {
        SdhciMark("WiFi: ");
        SdhciMark(Tag);
        SdhciMarkHex(": frame too big for one block, len=", Padded, 4);
        SdhciMark("\n");
        return -1;
    }
    for (Index = Aligned; Index < Padded; Index++) {
        Buffer[Index] = 0;
    }
    if (DataPathFlowControl(Host, &Credit, Tag) != 0) {
        SdhciMark("WiFi: ");
        SdhciMark(Tag);
        SdhciMark(" FAILED at flow control: fc=");
        SdhciMarkHex("", Credit, 2);
        SdhciMark(" need>=");
        SdhciMarkHex("", AIC_DATA_PATH_FLOW_CONTROL_MINIMUM, 2);
        SdhciMark(" (chip has no free data buffer; nothing was written)\n");
        return -1;
    }
    SdhciMark("WiFi: ");
    SdhciMark(Tag);
    SdhciMarkHex(": fc=", Credit, 2);
    SdhciMarkHex(" payload=", Length, 4);
    SdhciMarkHex(" padded=", Padded, 4);
    SdhciMarkHex(" blocks=", Padded / AIC_DATA_PATH_BLOCK, 2);
    SdhciMark("\n");
    if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, Padded / AIC_DATA_PATH_BLOCK,
                          AIC_DATA_PATH_BLOCK, Buffer) != 0) {
        AicMarkXferParams(Host, FailText, Padded, Padded / AIC_DATA_PATH_BLOCK);
        (void)SdhciWifiBusRecover(Host);
        return -1;
    }
    return 0;
}
