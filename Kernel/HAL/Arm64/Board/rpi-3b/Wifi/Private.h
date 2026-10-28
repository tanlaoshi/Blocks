/*
 * Wifi/Private.h — Pi3B WiFi SDIO / 固件 / 扫描内部（pwifi-1/2）
 */
#ifndef BOARD_WIFI_PRIVATE_H
#define BOARD_WIFI_PRIVATE_H

#include "../Wifi.h"
#include "../Sdhci/Private.h"
#include "Board.h"
#include "Hal.h"
#include "HalDriverFile.h"
#include "HalSerial.h"
#include "PhysicalMemory.h"
#include "SerialLog.h"

/* SDIO CCCR / FBR（SDIO Simplified Spec） */
#define SDIO_FUNCTION_0              0u
#define SDIO_FUNCTION_1              1u
#define SDIO_FUNCTION_2              2u
#define SDIO_CCCR_IO_ENABLE          0x02u
#define SDIO_CCCR_IO_READY           0x03u
#define SDIO_CCCR_INT_ENABLE         0x04u
#define SDIO_CCCR_IO_ABORT           0x06u /* SDIO Simplified Spec：ASx，勿置 RES */
#define SDIO_CCCR_BUS_INTERFACE      0x07u
#define SDIO_CCCR_HIGH_SPEED         0x13u
#define SDIO_FBR1_BLOCK_SIZE         0x110u
#define SDIO_FBR2_BLOCK_SIZE         0x210u

#define SDIO_COMMAND_IO_SEND_OP_COND 5u
#define SDIO_COMMAND_IO_RW_DIRECT    52u
#define SDIO_COMMAND_IO_RW_EXTENDED  53u

#define SDIO_OCR_VOLTAGE_3V3         (3u << 20) /* 3.2–3.4 V */
#define SDIO_OCR_CARD_READY          (1u << 31)

/* Broadcom SDIO backplane（对表 brcmfmac / cyw43） */
#define SDIO_BACKPLANE_ADDRESS       0x1000Au
#define SDIO_BACKPLANE_WINDOW_SIZE   0x8000u
#define SDIO_BACKPLANE_ACCESS_32BIT  0x8000u
#define SDIO_FUNCTION2_WATERMARK     0x10008u
#define SDIO_CHIP_CLOCK_CONTROL      0x1000Eu

#define WIFI_CHIPCOMMON_BASE         0x18000000u
/* 对表 Linux cores_check：D11 base 0x18001000 wrap 0x18101000（勿把 wrap 当 core） */
#define WIFI_D11_CORE_BASE           0x18001000u
#define WIFI_SDIO_CORE_BASE          0x18002000u
#define WIFI_ARM_CORE_BASE           0x18003000u
#define WIFI_SOCRAM_CORE_BASE        0x18004000u
#define WIFI_WRAPPER_OFFSET          0x100000u
#define WIFI_D11_WRAPPER             (WIFI_D11_CORE_BASE + WIFI_WRAPPER_OFFSET)
/* BCM283x Clock Manager：GPCLK2 → GPIO43 = WiFi/BT 32.768 kHz LPO */
#define WIFI_CLOCK_MANAGER_BASE      0x3F101000ULL
#define WIFI_CLOCK_MANAGER_PASSWORD  0x5A000000u
#define WIFI_CLOCK_MANAGER_GP2_CONTROL_OFFSET 0x80u
#define WIFI_CLOCK_MANAGER_GP2_DIVIDER_OFFSET 0x84u
#define WIFI_CLOCK_MANAGER_ENABLE    0x10u
#define WIFI_CLOCK_MANAGER_BUSY      0x80u
#define WIFI_CLOCK_MANAGER_SOURCE_OSCILLATOR 1u
/* 19.2 MHz / 586 ≈ 32764 Hz（对表 BCM2835 osc → eLPO） */
#define WIFI_CLOCK_32K_DIVIDER_INTEGER 586u
#define WIFI_ARM_WRAPPER             (WIFI_ARM_CORE_BASE + WIFI_WRAPPER_OFFSET)
#define WIFI_SOCRAM_WRAPPER          (WIFI_SOCRAM_CORE_BASE + WIFI_WRAPPER_OFFSET)
#define WIFI_SOCRAM_BANK_INDEX       (WIFI_SOCRAM_CORE_BASE + 0x10u)
#define WIFI_SOCRAM_BANK_PDA         (WIFI_SOCRAM_CORE_BASE + 0x44u)
#define WIFI_D11_PHY_RESET           0x0008u
#define WIFI_D11_PHY_CLOCK_ENABLE    0x0004u
#define WIFI_CLOCK_REQUEST_HT        0x10u
#define WIFI_SDIO_INT_STATUS         (WIFI_SDIO_CORE_BASE + 0x20u) /* sdpcmd_regs */
#define WIFI_SDIO_INT_HOST_MASK      (WIFI_SDIO_CORE_BASE + 0x24u)
#define WIFI_SDIO_FUNCTION_INT_MASK  (WIFI_SDIO_CORE_BASE + 0x34u)
#define WIFI_SDIO_TO_SB_MAILBOX      (WIFI_SDIO_CORE_BASE + 0x40u)

#define WIFI_AI_IO_CONTROL_OFFSET    0x408u
#define WIFI_AI_RESET_CONTROL_OFFSET 0x800u
#define WIFI_AI_RESET_BIT            1u
#define WIFI_SICF_CLOCK_ENABLE       0x0001u
#define WIFI_SICF_FORCE_GATED_CLOCK  0x0002u
#define WIFI_SICF_CPU_HALT           0x0020u

#define WIFI_CLOCK_FORCE_ALP         0x01u
#define WIFI_CLOCK_FORCE_HT          0x02u
#define WIFI_CLOCK_REQUEST_ALP       0x08u
#define WIFI_CLOCK_NO_HARDWARE_REQ   0x20u
#define WIFI_CLOCK_ALP_AVAILABLE     0x40u
#define WIFI_CLOCK_HT_AVAILABLE      0x80u
#define WIFI_HOST_MAILBOX_SOFTWARE_MASK 0x000000F0u
#define WIFI_FUNCTION2_WATERMARK_VALUE  8u

#define WIFI_CHIP_ID_43455           0x4345u
#define WIFI_CHIP_ID_43430           0xA9A6u
#define WIFI_RAM_SIZE                (512u * 1024u)
/* 灌片 CMD53 块长；4B@0 曾 write fail，64B+|0x8000 能灌完（HT 另查） */
#define WIFI_BACKPLANE_BLOCK_BYTES   64u
#define WIFI_WLAN_BLOCK_BYTES        512u
#define WIFI_FIRMWARE_MAX_BYTES      (512u * 1024u)
#define WIFI_NVRAM_MAX_BYTES         4096u

#define WIFI_FIRMWARE_PATH_43455 \
    "Assets/Firmware/brcm/brcmfmac43455-sdio.bin"
#define WIFI_FIRMWARE_PATH_43430_A0 \
    "Assets/Firmware/brcm/brcmfmac43430a0-sdio.bin"
#define WIFI_FIRMWARE_PATH_43430_A1 \
    "Assets/Firmware/brcm/brcmfmac43430-sdio.bin"
#define WIFI_NVRAM_PATH_43430 \
    "Assets/Firmware/brcm/brcmfmac43430-sdio.txt"

#define WIFI_GPIO_BASE               0x3F200000ULL
#define WIFI_IDENTIFY_HERTZ          400000u
#define WIFI_TRANSFER_HERTZ          25000000u

/* SDPCM / IOCTL（对表 cyw43 / wlioctl） */
#define WIFI_SDPCM_HEADER_BYTES      12u
#define WIFI_IOCTL_HEADER_BYTES      16u
#define WIFI_SDPCM_CONTROL_CHANNEL   0u
#define WIFI_SDPCM_EVENT_CHANNEL     1u
#define WIFI_SDPCM_DATA_CHANNEL      2u
#define WIFI_SDPCM_GET               0u
#define WIFI_SDPCM_SET               2u
#define WIFI_IOCTL_ID_SHIFT          16u
#define WIFI_WLC_UP                  2u
#define WIFI_WLC_SET_SSID            26u
#define WIFI_WLC_GET_VAR             262u
#define WIFI_WLC_SET_VAR             263u
#define WIFI_WLC_GET_CHANNEL         29u
#define WIFI_WLC_SET_SCAN            50u /* legacy；本刀用 escan iovar */
#define WIFI_SCAN_RESULT_MAXIMUM     32u
#define WIFI_SSID_MAXIMUM_BYTES      BOARD_WIFI_SSID_MAXIMUM

extern int gWifiChipProbed;
extern int gWifiFirmwarePresent;
extern int gWifiFirmwareLoaded;
extern UINT32 gWifiChipIdentity;
extern UINT32 gWifiChipRevision;
extern UINT32 gWifiRelativeAddress;
extern UINT32 gWifiBackplaneWindow;
extern UINT8 gWifiSdpcmSequence;
extern UINT8 gWifiSdpcmBusCredit;
extern UINT16 gWifiIoctlRequestIdentity;
extern UINT8 gWifiSdpcmPacket[2048];

extern BOARD_WIFI_SCAN_ENTRY gWifiScanEntries[WIFI_SCAN_RESULT_MAXIMUM];
extern UINT32 gWifiScanEntryCount;

void WifiPinConfigure(void);
int WifiWirelessPowerOn(void);
int WifiSdioHostBringUp(void);
int WifiSdioCommand52Read(UINT32 Function, UINT32 RegisterAddress, UINT8 *OutData);
int WifiSdioCommand52Write(UINT32 Function, UINT32 RegisterAddress, UINT8 Data);
int WifiSdioCommand53Read(UINT32 Function, UINT32 RegisterAddress, UINT8 *Buffer,
                          UINT32 ByteCount, int AddressIncrement);
int WifiSdioCommand53Write(UINT32 Function, UINT32 RegisterAddress, const UINT8 *Buffer,
                           UINT32 ByteCount, int AddressIncrement);
int WifiSdioEnumerate(void);
int WifiSdioEnableFunction1(void);
int WifiSdioEnableFunction2(void);
int WifiChipIdentityProbe(void);
const char *WifiFirmwarePathForChip(void);
const char *WifiNvramPathForChip(void);
void WifiFirmwarePathProbe(void);
void WifiBootMarkHexadecimal32(const char *Tag, UINT32 Value);

int WifiBackplaneWindowSet(UINT32 Address);
void WifiBackplaneWindowInvalidate(void);
int WifiBackplaneRead32(UINT32 Address, UINT32 *OutValue);
int WifiBackplaneWrite32(UINT32 Address, UINT32 Value);
int WifiBackplaneWriteMemory(UINT32 Address, const UINT8 *Data, UINT32 ByteCount);
/* 对表 Linux brcmf_sdiod_ramrw：窗内偏移 | ACCESS_2_4B（0x8000） */
int WifiBackplaneReadMemory(UINT32 Address, UINT8 *Data, UINT32 ByteCount);
int WifiCoreDisable(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits);
int WifiCoreReset(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits,
                  UINT32 PostResetBits);
/* 灌后 CMD53 读楔死时用：只写、延时，对表 cyw43 reset_device_core 写序 */
int WifiCoreResetWritesOnly(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits,
                            UINT32 PostResetBits);
int WifiCoreIsUp(UINT32 WrapperBase);

int WifiNvramPrepare(const UINT8 *Text, UINTN TextBytes, UINT8 *Out, UINT32 OutCapacity,
                     UINT32 *OutBytes);
int WifiVerifyFirmwareWord(const UINT8 *Firmware);
int WifiExitDownload(void);
int WifiWaitHtAndFunction2(void);
int WifiSdioHostCommandDataRecover(void); /* F1 abort + CMD|DATA 复位 */
int WifiFirmwareLoad(void); /* 0=ok；须 FS 已挂 */
int WifiIoctl(UINT32 Kind, UINT32 Command, UINT8 *Payload, UINT32 PayloadBytes,
              UINT32 *OutPayloadBytes);
int WifiIoctlSetVariable(const char *Name, const UINT8 *Value, UINT32 ValueBytes);
int WifiRadioBringUp(void); /* event_msgs + WLC_UP */
int WifiScanAccessPoints(void); /* 填 gWifiScanEntries；须固件已灌 */
int WifiAssociateOpen(const char *Ssid); /* 开放网关联 */

#endif
