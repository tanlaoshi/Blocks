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

/* ---- 板级（Power.c）：时钟门控 + 上电 + 卡检测 ---- */
#define DUO_TOP_BASE 0x03000000ULL
#define DUO_TOP_SD_CTRL_OPT 0x0294u
/*
 * 时钟门控（TOP CRG @0x03002000，见 cv181x_base.dtsi:26-28）：
 * clk-cv181x.c:551-575 里 clk_axi4_sd1=bit21、clk_sd1=bit22、clk_100k_sd1=bit23，
 * 都带 CLK_IS_CRITICAL；Linux 由 CCF 打开，我们没有 CCF，自己开。
 */
#define DUO_CLK_BASE 0x03002000ULL
#define DUO_CLK_EN_0 0x000u
#define DUO_CLK_SD1_AXI4_BIT (1u << 21)
#define DUO_CLK_SD1_CARD_BIT (1u << 22)
#define DUO_CLK_SD1_100K_BIT (1u << 23)
#define DUO_PINMUX_BASE 0x03001000ULL /* FMUX GPIO；SD1_* 在 +0xD0..0xE4 */
#define DUO_GPIOA_BASE 0x03020000ULL
/* port E 不在 0x0302_x000 那一排：dts cv181x_base_riscv.dtsi:246 = gpio@05021000 */
#define DUO_GPIOE_BASE 0x05021000ULL
#define DUO_GPIO_SWPORT_DR 0x0000u
#define DUO_GPIO_SWPORT_DDR 0x0004u
#define DUO_WIFI_POWER_BIT (1u << 15) /* GPIOA15，dts &wifi_pin poweron-gpio */
#define DUO_WIFI_WAKE_BIT (1u << 7)   /* PE7，dts &wifi_pin wakeup-gpio（厂商不用） */
/* 厂商 aicsdio.c:519 mdelay(50)；断电脉冲是我们加的一次干净 POR */
#define DUO_WIFI_POWER_DOWN_MS 100u
#define DUO_WIFI_POWER_UP_MS 50u
/* 厂商 aicsdio.c:534 给芯片 2000 ms 反应窗口（down_timeout 2000 ms） */
#define DUO_WIFI_UP_WINDOW_MS 2000u
/* CMD5 单次尝试 ≈100 ms 超时 + 50 ms 间隔 → 12 次 ≈1.8 s，落在 2 s 窗口内 */
#define DUO_WIFI_CMD5_TRIES 12u
#define DUO_WIFI_CMD5_GAP_MS 50u
int DuoWifiClockGate(void);
int DuoWifiPowerOn(void);
int DuoWifiCardDetect(SDHCI_HOST *Host);
void DuoWifiWakePeek(void);
void DuoWifiPadPeek(void);
void DuoWifiWakeDrive(int Level);

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
/* S5b：SDIO 枚举成功的握手接口（Sdio.c 导出给 WifiFw.c） */
int SdhciWifiReady(void);
int SdhciWifiChipIsD80(void);
int SdhciWifiBusRecover(SDHCI_HOST *Host);
int SdhciHostInitialize(SDHCI_HOST *Host);

/* ---- SDIO 枚举（Sdio.c / SdioCis.c / SdioTransfer.c / SdioSelfTest.c） ---- */
/* CCCR / FBR 偏移（include/linux/mmc/sdio.h） */
#define SDIO_CCCR_CCCR 0x00u
#define SDIO_CCCR_IOEX 0x02u
#define SDIO_CCCR_IORX 0x03u
#define SDIO_CCCR_IF 0x07u
#define SDIO_CCCR_CAPS 0x08u
#define SDIO_CCCR_SPEED 0x13u
#define SDIO_FBR_CIS 0x09u
#define SDIO_FBR_BLKSIZE 0x10u
#define SDIO_BUS_WIDTH_MASK 0x03u
#define SDIO_BUS_WIDTH_4BIT 0x02u
#define SDIO_SPEED_SHS 0x01u
#define SDIO_SPEED_EHS 0x02u
#define SDIO_CAP_SDC 0x01u
#define SDIO_CAP_SMB 0x02u
#define SDIO_CAP_4BLS 0x80u
/* CIS tuple（sdio_cis.c:235 的 TPLMID 表） */
#define CISTPL_MANFID 0x20u
#define CISTPL_FUNCID 0x21u
#define CISTPL_FUNCE 0x22u
/* AIC fn1 私有寄存器（aicsdio.h:36-48，D80/V3 表） */
#define AIC_BYTEMODE_ENABLE_REG_V3 0x07u
#define AIC_MISC_INT_STATUS_REG_V3 0x04u
#define AIC_RD_FIFO_ADDR_V3 0x0Fu
#define AIC_WR_FIFO_ADDR_V3 0x10u
/* 自检：1536 B = 3 块 × 512（固件块消息的线上长度，时序表 §2.6） */
#define SDIO_SELFTEST_BYTES 1536u
#define SDIO_SELFTEST_BLOCKS (SDIO_SELFTEST_BYTES / SDHCI_BLOCK_BYTES)

typedef struct SDIO_CIS_INFO {
    UINT16 Vendor;
    UINT16 Device;
    UINT8 FunctionClass;
    UINT16 MaxBlockSize;
    int SawManfid;
} SDIO_CIS_INFO;

/*
 * SdhciWifiInitialize 的步骤上下文：原函数的局部变量集中在这里，主函数只声明
 * 一个、按指针传给每一步。步骤分散在 SdioInit.c / SdioTransfer.c / SdioCis.c，
 * 所以类型放本头（不是 Sdio.c 私有）。
 */
typedef struct WIFI_PROBE {
    SDHCI_HOST *Host;
    UINT32 Ocr;
    UINT32 Offer;
    UINT32 NoResponse;
    UINT16 Flags;
    UINT16 Version;
    UINT8 Data;
    UINT8 Speed;
    UINT8 Caps;
    UINT32 Present;
    SDIO_CIS_INFO Cis;
    int CisSeen;
    UINT32 CisCommonPtr;
    int CisOk;
} WIFI_PROBE;

/*
 * 步骤函数：0 = 继续（或本步成功），非 0 = 已失败（内部已打日志并置
 * gWifiSdioState）。ProbePrepare 例外：1 = 上次已成功、主函数直接返回 0。
 */
int ProbePrepare(WIFI_PROBE *P);
int ControllerBringUp(WIFI_PROBE *P);
int ModulePowerOn(WIFI_PROBE *P);
int CardEnumerate(WIFI_PROBE *P);
int CccrConfigure(WIFI_PROBE *P);
int SpeedSwitch(WIFI_PROBE *P);
int BusWidthSwitch(WIFI_PROBE *P);
int FunctionEnable(WIFI_PROBE *P);
int CisVerify(WIFI_PROBE *P);
int CisReport(WIFI_PROBE *P);
int ProbeFinish(WIFI_PROBE *P);
int SdioFail(const char *Where);

extern int gWifiSdioState;
extern UINT16 gWifiChipVendor;
extern UINT16 gWifiChipDevice;
extern UINT8 gWifiHostControl;
extern UINT32 gWifiRestoreHertz;
extern UINT8 gSdioPattern[SDIO_SELFTEST_BYTES];
extern UINT8 gSdioReadback[SDIO_SELFTEST_BYTES];

UINT32 SdioScanCis(SDHCI_HOST *Host, UINT32 From, UINT32 To);
int SdioChipIdentify(void);
int SdioCmd5Window(SDHCI_HOST *Host);
int SdioFifoTest(SDHCI_HOST *Host, int MultiBlock);
int SdioFifoTransfer(SDHCI_HOST *Host, int Write, UINT8 *Buffer, int MultiBlock);
int SdioParseCis(SDHCI_HOST *Host, UINT32 Pointer, SDIO_CIS_INFO *Info);
int SdioReadCisPointer(SDHCI_HOST *Host, UINT32 Function, UINT32 *Pointer);
int SdioRecoverBus(SDHCI_HOST *Host, UINT8 HostControl, UINT32 RestoreHertz);
void SdioDumpCis(SDHCI_HOST *Host, const char *Tag, UINT32 Address, UINT32 Count);
void SdioMarkCis(UINT32 Function, UINT32 Pointer, const SDIO_CIS_INFO *Info);
void SdioMarkCmd5State(SDHCI_HOST *Host, const char *Tag);
void SdioMarkFn1Regs(SDHCI_HOST *Host);
void SdioMarkXfer(SDHCI_HOST *Host, const char *Tag);

#endif
