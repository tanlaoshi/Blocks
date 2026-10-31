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
/* Broadcom CCCR 厂商区：对表 brcmfmac sdio.h / probe_attach */
#define SDIO_CCCR_BROADCOM_CARD_CONTROL                0xF1u
#define SDIO_CCCR_BROADCOM_CARD_CONTROL_WLAN_RESET     0x02u
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
#define SDIO_PULL_UP_CONTROL         0x1000Fu
#define WIFI_SDIO_DEVICE_CONTROL     0x10009u
#define WIFI_SDIO_WAKEUP_CONTROL     0x1001Eu
#define WIFI_SDIO_SLEEP_CONTROL      0x1001Fu

#define WIFI_CHIPCOMMON_BASE         0x18000000u
/* chipcregs.pmu*；对表 bcma_driver_chipcommon.h / BCMA_CC_PMU_* */
#define WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_CONTROL_OFFSET 0x600u
#define WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_CONTROL_RESOURCE_RELOAD \
    (0x2u << 13)
#define WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_STATUS_OFFSET 0x608u
#define WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_RESOURCE_STATUS_OFFSET 0x60Cu
#define WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_MINIMUM_RESOURCE_MASK_OFFSET \
    0x618u
#define WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_MAXIMUM_RESOURCE_MASK_OFFSET \
    0x61Cu
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

#define WIFI_AI_IO_CONTROL_OFFSET     0x408u
#define WIFI_AI_RESET_CONTROL_OFFSET  0x800u
#define WIFI_AI_RESET_BIT             1u
#define WIFI_SICF_CLOCK_ENABLE        0x0001u
#define WIFI_SICF_FORCE_GATED_CLOCK   0x0002u
#define WIFI_SICF_CPU_HALT            0x0020u

#define WIFI_CLOCK_FORCE_ALP         0x01u
#define WIFI_CLOCK_FORCE_HT          0x02u
#define WIFI_CLOCK_REQUEST_ALP       0x08u
#define WIFI_CLOCK_NO_HARDWARE_REQ   0x20u
#define WIFI_CLOCK_ALP_AVAILABLE     0x40u
#define WIFI_CLOCK_HT_AVAILABLE      0x80u
#define WIFI_HOST_MAILBOX_SOFTWARE_MASK 0x000000F0u
#define WIFI_FUNCTION2_WATERMARK_VALUE  8u

/* SDHCI PRESENT_STATE bit2：DAT 线仍被卡占用（写块后 busy） */
#define WIFI_HOST_DAT_LINE_ACTIVE    0x00000004u
/* R5_STATUS：仅 ERROR 位（卡侧 function busy），非地址/CRC 类错 */
#define WIFI_SDIO_R5_ERROR_ONLY      0x0800u
#define WIFI_SDIO_R5_ERROR_MASK      0xCB00u

#define WIFI_CHIP_ID_43455           0x4345u
#define WIFI_CHIP_ID_43430           0xA9A6u
#define WIFI_RAM_SIZE                (512u * 1024u)
/* 灌片 CMD53 块长；对表 Linux mmc1 ftrace 众数 block_size=64 */
#define WIFI_BACKPLANE_BLOCK_BYTES   64u
/*
 * 单条 CMD53 最大块数（SDIO 9-bit）。Linux brcmf 灌片写众数 = 511×64（≈32704 B），
 * 不是 32×64（那是读侧众数）。见 Archive/过程/排障/Pi-Linux-brcmf-20261031。
 */
#define WIFI_BACKPLANE_BLOCKS_PER_COMMAND 511u
/*
 * 诊断用别名之一。Linux / wifi-3 探针均以 @0 为主；满灌基址由
 * WifiSramFindAccessibleBase() 运行时选定（优先 0）。
 */
#define WIFI_RAM_DOWNLOAD_BASE       0x1e000000u
/* 每改一版就 +1：串口/BOOT.LOG 第一行会打出来，避免「烧的是不是新镜像」这类歧义 */
#define WIFI_BUILD_TAG               0x00000155u
/* 块模式总开关：置 0 时全走字节模式（一次最多 512 字节），用来隔离“等待时间”变量 */
/* 1 = 对表 Linux：块模式批量（≤511 块）；余数仍块模式，勿对整块走字节 CMD53 */
#define WIFI_BACKPLANE_BLOCK_MODE    1
/* SDHCI 传输模式 bit5：多块（Sdhci.h 未定义，只在 Wifi 内用） */
#define WIFI_SDHCI_TRANSFER_MULTI    (1u << 5)
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
/*
 * 传输钟：旧值 25 MHz（default speed）。Linux 真机 mmc1 ios
 * （Source/PI/wifi/board/pi-mmc1-ios.txt）是 50 MHz / sd high-speed / 4-bit。
 * tag 0x123（Docs/04 刀 A）与之对齐；CCCR EHS 失败时退回 25 MHz。
 */
#define WIFI_TRANSFER_HERTZ          50000000u
#define WIFI_TRANSFER_HERTZ_FALLBACK 25000000u

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
#define WIFI_WLC_GET_UP              162u /* wlioctl：GET_UP；误用 6=DUMP→BCME_UNSUPPORTED(-23) */
#define WIFI_WLC_SET_SSID            26u
#define WIFI_WLC_GET_VAR             262u
#define WIFI_WLC_SET_VAR             263u
#define WIFI_WLC_GET_CHANNEL         29u
#define WIFI_WLC_SET_SCAN            50u /* legacy；本刀用 escan iovar */
/* CDC status：-4 = BCME_NOTUP（0x145 CH） */
#define WIFI_BCME_NOTUP              0xFFFFFFFCu
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
extern UINT32 gWifiIoctlLastStatus; /* 0x145：最近一次 CDC status */
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
/* 块模式写（CMD53 bit27=1）：对表 brcmf_sdiod_ramrw → sdio_memcpy_toio */
int WifiSdioBlockWrite(UINT32 Function, UINT32 RegisterAddress, const UINT8 *Buffer,
                       UINT32 ByteCount, UINT32 BlockBytes);
int WifiSdioReadFunctionMaxBlockSize(void);
int WifiSdioReadFunctionBlockSize(void);
int WifiSdioSetFunctionBlockSize(UINT32 Bytes);
/* 传输层内部件：块模式写与字节模式写共用 */
int WifiSdioWaitCardIdle(void);
int WifiSdioWaitCardIdleMilliseconds(UINT32 Milliseconds);
void WifiSdioResetWaitReport(void);
void WifiDiagnosticsSramAccessProbe(void);
void WifiDiagnosticsSocramState(void);
void WifiDiagnosticsClockState(void);
UINT32 WifiSramFindAccessibleBase(void);
int WifiClockRequestHighThroughputAndWait(void);
int WifiBackplaneWriteReadbackTest(void);
int WifiSdioProgrammedIoWriteBytes(const UINT8 *Buffer, UINT32 ByteCount);
void WifiSdioProgrammedIoFailCleanup(void);
int WifiSdioEnumerate(void);
int WifiSdioEnableFunction1(void);
int WifiSdioEnableFunction2(void);
/* pwifi-2 解封第 1 步：只读打印 KSO / GPCLK2 / SDIO core rev */
void WifiBringUpDiagnosticsPrint(void);
int WifiChipIdentityProbe(void);
const char *WifiFirmwarePathForChip(void);
const char *WifiNvramPathForChip(void);
void WifiFirmwarePathProbe(void);
int WifiClmLoad(void); /* 0x14b：F2 后灌 clm_blob */
void WifiBootMarkHexadecimal32(const char *Tag, UINT32 Value);

int WifiSdioSleepControlEnsureAwake(void); /* F1 SLEEPCSR KSO；灌片前必醒 */
int WifiAlpRequest(void); /* buscoreprep → csr 含 FORCE_ALP（0x21） */
int WifiAlpReleaseForceToRequest(void); /* 探针后、整包灌片前：清 FORCE → REQUEST_ALP */
/* 对表 brcmf_sdio_probe_attach：CARDCTRL WLANRESET + PMU RES_RELOAD */
int WifiSdioSetupResetBehavior(void);
int WifiPowerManagementUnitForceResources(void); /* min/max←ffffffff */
int WifiSocramResetAfterResources(void);         /* 资源齐后再 CoreReset+bankpda */
int WifiEnterDownload(void);
int WifiBackplaneWindowSet(UINT32 Address);
void WifiBackplaneWindowInvalidate(void);
int WifiBackplaneRead32(UINT32 Address, UINT32 *OutValue);
int WifiBackplaneWrite32(UINT32 Address, UINT32 Value);
int WifiBackplaneWriteMemory(UINT32 Address, const UINT8 *Data, UINT32 ByteCount);
/* 对表 Linux brcmf_sdiod_ramrw：窗内偏移 | ACCESS_2_4B（0x8000） */
int WifiBackplaneReadMemory(UINT32 Address, UINT8 *Data, UINT32 ByteCount);
int WifiBackplaneSramProbeWrite(void); /* 整包灌片前；失败见 WifiSramProbeFailStage */
const char *WifiSramProbeFailStage(void); /* 0x155：load-sram-write/read/<got> */
int WifiCoreDisable(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits);
int WifiCoreReset(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits,
                  UINT32 PostResetBits);
int WifiCoreIsUp(UINT32 WrapperBase);

int WifiNvramPrepare(const UINT8 *Text, UINTN TextBytes, UINT8 *Out, UINT32 OutCapacity,
                     UINT32 *OutBytes);
int WifiVerifyDownloadPath(void);
int WifiFirmwareSampleReadback(UINT32 DownloadBase, const UINT8 *FirmwareBlob,
                               UINT32 FirmwareBytes);
int WifiExitDownload(void);
int WifiWaitHighThroughput(void); /* pi-wifi-5：等 HT_AVAILABLE；超时打 csr */
int WifiWaitHtAndFunction2(void); /* HT + F2；wifi-6 再用 */
int WifiSdioHostCommandDataRecover(void); /* F1 abort + CMD|DATA 复位 */
int WifiSdioBusRejoinAfterIdle(void); /* WL_ON POR+枚举+F1；延后 Claim 灌片前 */
int WifiFirmwareLoad(void); /* 0=ok；须 FS 已挂 */
/* 0x148：claim-load-fail 时 CH 看具体阶段（rejoin/kso/ht/…） */
const char *WifiFirmwareLoadFailStage(void);
int WifiIoctl(UINT32 Kind, UINT32 Command, UINT8 *Payload, UINT32 PayloadBytes,
              UINT32 *OutPayloadBytes);
int WifiIoctlSetVariable(const char *Name, const UINT8 *Value, UINT32 ValueBytes);
int WifiRadioBringUp(void); /* country + event_msgs + WLC_UP */
const char *WifiRadioFailStatus(void); /* 0x147：country-fail / up-fail / getup-fail */
int WifiScanAccessPoints(void); /* 填 gWifiScanEntries；须固件已灌 */
/* 0x144：Scan 失败细分状态字（escan-fail / scan-empty）；成功为 0 */
const char *WifiScanFailStatus(void);
UINT32 WifiScanPollEmptyCount(void);
UINT32 WifiScanPollErrorCount(void);
UINT32 WifiScanEventPacketCount(void);
UINT32 WifiScanEscanPartialCount(void);
UINT32 WifiScanEtherTypeOkCount(void); /* 0x14e */
UINT32 WifiScanFirstBdcDataOffset(void);
UINT32 WifiScanFirstEventTypeAt2(void);
UINT32 WifiScanFirstEventTypeAt4(void);
UINT32 WifiScanFirstEventStatusAt6(void);
UINT32 WifiScanFirstEventStatusAt8(void);
UINT32 WifiScanFirstEventLength(void);
void WifiScanPollCountersReset(void);
void WifiScanPollEmptyBump(void);
void WifiScanPollErrorBump(void);
void WifiScanEventPacketBump(void);
void WifiScanEventSampleNote(UINT8 BdcDataOffset, UINT16 EtherType, const UINT8 *Event,
                             UINT32 EventLength); /* 0x14e：只记首包 */
int WifiPollOnePacket(UINT8 *OutChannel, UINT32 *OutOffset, UINT32 *OutLength);
int WifiHandleEscanEvent(const UINT8 *Event, UINT32 Length);
int WifiAssociateOpen(const char *Ssid); /* 开放网关联 */
int WifiJoinAfterScan(void); /* 0x150：扫到课网则 WPA2-PSK */
int WifiSdpcmSendEthernet(const UINT8 *Frame, UINT32 FrameBytes); /* 对表 cyw43 send_ethernet */
int WifiSdpcmSendArpWhoHas(const UINT8 SourceMac[6], UINT32 TargetProtocolAddress);
int WifiNetProbeAfterJoin(void); /* 0x154：TX 与被动 RX 解耦的 ARP 眼睛 */

#endif
