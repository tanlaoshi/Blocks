/*
 * Wifi/Private.h — Pi3B WiFi SDIO 探针内部（pwifi-1）
 */
#ifndef BOARD_WIFI_PRIVATE_H
#define BOARD_WIFI_PRIVATE_H

#include "../Wifi.h"
#include "../Sdhci/Private.h"
#include "Board.h"
#include "Hal.h"
#include "HalDriverFile.h"
#include "HalSerial.h"
#include "SerialLog.h"

/* SDIO CCCR / FBR（SDIO Simplified Spec） */
#define SDIO_FUNCTION_0              0u
#define SDIO_FUNCTION_1              1u
#define SDIO_CCCR_IO_ENABLE          0x02u
#define SDIO_CCCR_IO_READY           0x03u
#define SDIO_CCCR_INT_ENABLE         0x04u
#define SDIO_CCCR_BUS_INTERFACE      0x07u
#define SDIO_CCCR_HIGH_SPEED         0x13u
#define SDIO_FBR1_BLOCK_SIZE         0x110u

#define SDIO_COMMAND_IO_SEND_OP_COND 5u
#define SDIO_COMMAND_IO_RW_DIRECT    52u
#define SDIO_COMMAND_IO_RW_EXTENDED  53u

#define SDIO_OCR_VOLTAGE_3V3         (3u << 20) /* 3.2–3.4 V */
#define SDIO_OCR_CARD_READY          (1u << 31)

/* Broadcom SDIO backplane 窗（对表 brcmfmac / Circle） */
#define SDIO_BACKPLANE_ADDRESS       0x1000Au
#define SDIO_BACKPLANE_WINDOW_SIZE   0x8000u
#define SDIO_BACKPLANE_ACCESS_32BIT  0x8000u
#define SDIO_CHIPCOMMON_BASE         0x18000000u
#define SDIO_CLOCK_CONTROL_STATUS    0x1000Eu
#define SDIO_CLOCK_FORCE_ALP         0x01u
#define SDIO_CLOCK_REQUEST_ALP       0x08u
#define SDIO_CLOCK_NO_HARDWARE_REQ   0x20u
#define SDIO_CLOCK_ALP_AVAILABLE     0x40u
#define SDIO_CLOCK_HT_AVAILABLE      0x80u

#define WIFI_CHIP_ID_43455           0x4345u /* BCM43455；sig 常见 0x15264345（3B+） */
#define WIFI_CHIP_ID_43430           0xA9A6u /* BCM43430；sig 常见 0x1541a9a6（本板 3B） */
#define WIFI_FIRMWARE_PATH_43455 \
    "Assets/Firmware/brcm/brcmfmac43455-sdio.bin"
#define WIFI_FIRMWARE_PATH_43430 \
    "Assets/Firmware/brcm/brcmfmac43430-sdio.bin"

#define WIFI_GPIO_BASE               0x3F200000ULL
#define WIFI_IDENTIFY_HERTZ          400000u
#define WIFI_TRANSFER_HERTZ          25000000u

extern int gWifiChipProbed;
extern int gWifiFirmwarePresent;
extern UINT32 gWifiChipIdentity;
extern UINT32 gWifiChipRevision;
extern UINT32 gWifiRelativeAddress;

void WifiPinConfigure(void);
int WifiWirelessPowerOn(void);
int WifiSdioHostBringUp(void);
int WifiSdioCommand52Read(UINT32 Function, UINT32 RegisterAddress, UINT8 *OutData);
int WifiSdioCommand52Write(UINT32 Function, UINT32 RegisterAddress, UINT8 Data);
int WifiSdioCommand53Read(UINT32 Function, UINT32 RegisterAddress, UINT8 *Buffer,
                          UINT32 ByteCount);
int WifiSdioEnumerate(void);
int WifiSdioEnableFunction1(void);
int WifiChipIdentityProbe(void);
const char *WifiFirmwarePathForChip(void); /* 按 chip id；未知则 0 */
void WifiFirmwarePathProbe(void);
void WifiBootMarkHexadecimal32(const char *Tag, UINT32 Value);

#endif
