/*
 * Sdhci/Private.h — Duo SDHCI 内部（bd-duo-sd / duo-wifi-sdio）
 *
 * 所有寄存器访问都吃 SDHCI_HOST*；不再有单实例全局量（PR-30 S1）。
 */
#ifndef BOARD_SDHCI_PRIVATE_H
#define BOARD_SDHCI_PRIVATE_H

#include "../Sdhci.h"
#include "Board.h"
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Hal.h"
#include "HalSerial.h"

/* 识别 400 kHz；SD0 传输 25 MHz（未与卡协商 HS，保持原样） */
#define IDENTIFY_HERTZ      400000u
#define SD_TRANSFER_HERTZ 25000000u
/*
 * SD1/SDIO 传输 50 MHz：厂商 MMC 核在 HS 分支硬编码 50 MHz
 * （linux sdio.c:427-440、:857-865），150 MHz 的 set_ios 段在 #if 0 里。
 * 375 MHz 基钟只能整除出 46.875 MHz（div=8），49.999 拿不到；
 * 打印的 "clock=50MHz" 是协商目标，实际值另打一行。
 */
#define TRANSFER_HERTZ    50000000u
#define SD_BASE_CLOCK_HERTZ 375000000u
#define AREA1_MASK 0xFFFu

#define SDHCI_BLOCK_BYTES 512u
#define SDHCI_BLOCK_WORDS (SDHCI_BLOCK_BYTES / 4u)

/* ---- 板级（Power.c）：WiFi 上电 + 卡检测覆盖 ---- */
#define DUO_TOP_BASE 0x03000000ULL
#define DUO_TOP_SD_CTRL_OPT 0x0294u
#define DUO_GPIOA_BASE 0x03020000ULL
#define DUO_GPIO_SWPORT_DR 0x0000u
#define DUO_GPIO_SWPORT_DDR 0x0004u
#define DUO_WIFI_POWER_BIT (1u << 15) /* GPIOA15，dts &wifi_pin poweron-gpio */
int DuoWifiPowerOn(void);
int DuoWifiCardDetectOverride(void);

/* ---- 寄存器 / 延时 / 打印 ---- */
volatile UINT32 *SdhciRegister32(SDHCI_HOST *Host, UINT32 Offset);
UINT64 SdhciReadTime(void);
void SdhciDelayMicroseconds(UINT32 Microseconds);
void SdhciDelayMilliseconds(UINT32 Milliseconds);
UINT32 SdhciRead32(SDHCI_HOST *Host, UINT32 Offset);
UINT8 SdhciRead8(SDHCI_HOST *Host, UINT32 Offset);
UINT16 SdhciRead16(SDHCI_HOST *Host, UINT32 Offset);
void SdhciWrite32(SDHCI_HOST *Host, UINT32 Offset, UINT32 Value);
void SdhciWrite8(SDHCI_HOST *Host, UINT32 Offset, UINT8 Value);
void SdhciWrite16(SDHCI_HOST *Host, UINT32 Offset, UINT16 Value);
void SdhciMark(const char *Text);
void SdhciMarkHex(const char *Tag, UINT32 Value, int Digits);
void SdhciMarkDecimal(const char *Tag, UINT32 Value);
int SdhciWait8(SDHCI_HOST *Host, UINT32 Offset, UINT8 Mask, int Set, UINT32 Milliseconds);
int SdhciWait32(SDHCI_HOST *Host, UINT32 Offset, UINT32 Mask, int Set, UINT32 Milliseconds);

/* ---- 复位 / PHY / 时钟 / 发令（Command.c） ---- */
int SdhciSoftwareReset(SDHCI_HOST *Host, UINT8 Mask);
int SdhciPhysicalAfterReset(SDHCI_HOST *Host);
int SdhciClockConfigure(SDHCI_HOST *Host, UINT32 Hertz);
int SdhciCommandSend(SDHCI_HOST *Host, UINT8 CommandIndex, UINT32 Argument, UINT16 Flags,
                     UINT16 TransferMode);

/* ---- PIO 数据相 / SD 块读写（Transfer.c） ---- */
int SdhciPioRead(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 Bytes);
int SdhciPioWrite(SDHCI_HOST *Host, const UINT8 *Buffer, UINT32 Bytes);
int SdhciReadOneBlock(SDHCI_HOST *Host, UINT32 LogicalBlockAddress, UINT8 *Buffer);
int SdhciWriteOneBlock(SDHCI_HOST *Host, UINT32 LogicalBlockAddress, const UINT8 *Buffer);

/* ---- SDIO 命令（Io.c）：CMD52 / CMD53 ---- */
int SdhciIoRwDirect(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, int Write, UINT8 Data,
                    UINT8 *Out);
int SdhciIoRead8(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, UINT8 *Out);
int SdhciIoWrite8(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, UINT8 Data);
/*
 * CMD53。Blocks > 0 → 块模式（BlockSize 一般 512）；Blocks == 0 → 字节模式，
 * 长度取 BlockSize（1..512）。Increment = 0 即 OP_CODE=0（固定地址，AIC FIFO 必须）。
 */
int SdhciIoRwExtended(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, int Write,
                      int Increment, UINT32 Blocks, UINT32 BlockSize, UINT8 *Buffer);

/* ---- 卡初始化 / SDIO 枚举（Card.c / Sdio.c） ---- */
void SdhciFail(SDHCI_HOST *Host, const char *What);
int SdhciControllerBringUp(SDHCI_HOST *Host, UINT32 Hertz, UINT32 IntEnable);
int SdhciCardInitialize(SDHCI_HOST *Host);
int SdhciHostInitialize(SDHCI_HOST *Host);

#endif
