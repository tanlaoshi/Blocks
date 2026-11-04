/*
 * WifiStationAddress.c — 本机 STA MAC：默认本地管理地址，bringup 后换成 efuse
 *
 * 单变量（Docs/20 §1f）：对表厂商 rwnx_main.c 在 ADD_IF 前用 MM_GET_MAC_ADDR
 * 的 efuse MAC；假说「硬编码 02:00:00:00:00:01 与芯片过滤/空口 addr2 不一致
 * → 加密单播失效」。查询失败或全 0 则保留默认，不挡 bringup。
 */
#include "Wifi/Private.h"
#include "Wifi/ScanPrivate.h"
#include "CString.h"

/* 启动占位；bringup 后换成 efuse，或厂商 dflt_mac（rwnx_main.c:6819） */
#define AIC_STATION_ADDRESS_LOCAL_UNICAST 0x02u
#define AIC_STATION_ADDRESS_LAST_BYTE 0x01u
/* 厂商 efuse 全 0 时的默认：88:00:33:77:10:99（同文件 get_random_bytes 前的初值） */
static const UINT8 sAicStationAddressVendorDefault[AIC_SCAN_MAC_LENGTH] = {
    0x88u, 0x00u, 0x33u, 0x77u, 0x10u, 0x99u};

static UINT8 sAicStationAddress[AIC_SCAN_MAC_LENGTH] = {
    AIC_STATION_ADDRESS_LOCAL_UNICAST, 0u, 0u, 0u, 0u, AIC_STATION_ADDRESS_LAST_BYTE};
static UINT32 sAicStationAddressLoaded;

const UINT8 *AicScanStationAddress(void) {
    return sAicStationAddress;
}

static void StationAddressMark(const char *Tag) {
    SdhciMark(Tag);
    SdhciMarkHex("", (UINT32)sAicStationAddress[0], 2);
    SdhciMarkHex(":", (UINT32)sAicStationAddress[1], 2);
    SdhciMarkHex(":", (UINT32)sAicStationAddress[2], 2);
    SdhciMarkHex(":", (UINT32)sAicStationAddress[3], 2);
    SdhciMarkHex(":", (UINT32)sAicStationAddress[4], 2);
    SdhciMarkHex(":", (UINT32)sAicStationAddress[5], 2);
    SdhciMark("\n");
}

int AicStationAddressLoadFromFirmware(SDHCI_HOST *Host) {
    UINT8 Confirm[32];
    UINT32 ConfirmLength = 0;
    UINT32 Index;
    UINT32 NonZero = 0u;
    /* 厂商 rwnx_msg_tx.c:1091：mm_get_mac_addr_req.get = 1（4 B）；plen=0 会拿回全 0 */
    UINT32 RequestGet = 1u;

    if (Host == 0) {
        return -1;
    }
    if (sAicStationAddressLoaded != 0u) {
        StationAddressMark("WiFi: station address already loaded ");
        return 0;
    }
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_GET_MAC_ADDR_REQ, (UINT16)AIC_TASK_MM, &RequestGet,
                         (UINT16)sizeof(RequestGet)) != 0) {
        SdhciMark("WiFi: station address: GET_MAC_ADDR send FAILED; use vendor default\n");
        (void)memcpy(sAicStationAddress, sAicStationAddressVendorDefault, AIC_SCAN_MAC_LENGTH);
        sAicStationAddressLoaded = 1u;
        StationAddressMark("WiFi: station address vendor default ");
        return -1;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_GET_MAC_ADDR_CFM, Confirm, sizeof(Confirm),
                              &ConfirmLength, 4u, AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: station address: no GET_MAC_ADDR_CFM; use vendor default\n");
        (void)memcpy(sAicStationAddress, sAicStationAddressVendorDefault, AIC_SCAN_MAC_LENGTH);
        sAicStationAddressLoaded = 1u;
        StationAddressMark("WiFi: station address vendor default ");
        return -1;
    }
    if (ConfirmLength < AIC_MM_GET_MAC_ADDR_LENGTH) {
        SdhciMarkHex("WiFi: station address: cfm too short plen=", ConfirmLength, 4);
        SdhciMark("; use vendor default\n");
        (void)memcpy(sAicStationAddress, sAicStationAddressVendorDefault, AIC_SCAN_MAC_LENGTH);
        sAicStationAddressLoaded = 1u;
        StationAddressMark("WiFi: station address vendor default ");
        return -1;
    }
    for (Index = 0; Index < AIC_MM_GET_MAC_ADDR_LENGTH; Index++) {
        NonZero |= (UINT32)Confirm[Index];
    }
    if (NonZero == 0u) {
        /* 对表厂商：efuse 全 0 时用 dflt_mac，不用我们以前的 02:00:00:00:00:01 */
        SdhciMark("WiFi: station address: efuse all-zero → vendor default 88:00:33:77:10:99\n");
        (void)memcpy(sAicStationAddress, sAicStationAddressVendorDefault, AIC_SCAN_MAC_LENGTH);
        sAicStationAddressLoaded = 1u;
        StationAddressMark("WiFi: station address vendor default ");
        return 0;
    }
    (void)memcpy(sAicStationAddress, Confirm, AIC_SCAN_MAC_LENGTH);
    sAicStationAddressLoaded = 1u;
    StationAddressMark("WiFi: station address from firmware (efuse → ADD_IF) ");
    return 0;
}
