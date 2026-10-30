/*
 * Sdhci.h — Milk-V Duo S SDHCI 门面：SD0 µSD @0x04310000、SD1 wifi-sd @0x04320000
 *
 * PR-30（S1-S3）：多实例 + SDIO 枚举。
 *   SD0：SD 卡（CMD0/8/55/41/2/3/7/16）→ 块设备（启动盘），行为保持不变。
 *   SD1：SDIO（CMD0/5/3/7/52/53）→ AIC8800D80，non-removable、无 CD 脚，
 *        靠 TOP 0x03000294[9:8] 覆盖成「有卡」。
 * 寄存器按 SD Host Controller 简化规格；厂商区（0x200 起）对齐
 * Linux drivers/mmc/host/cvitek/sdhci-cv181x.c（SD1 复位带 MSHC_CTRL[16]）。
 */
#ifndef BOARD_SDHCI_H
#define BOARD_SDHCI_H

#include "BootTypes.h"

#define SDHCI_BASE_SD0 0x04310000ULL
#define SDHCI_BASE_SD1 0x04320000ULL /* TRM 18.4：SD1 控制寄存器窗 0x04320000-0x0432FFFF */
#define SDHCI_HOST_COUNT 2u

/* 卡类型（Host->CardType） */
#define SDHCI_CARD_NONE 0u
#define SDHCI_CARD_SD 1u
#define SDHCI_CARD_SDIO 2u

/*
 * 一个 SDHCI 控制器实例。原来散在 Sdhci.c 的全局量全部收进来，
 * 同一份代码即可跑 SD0 与 SD1（PR-30 S1）。
 */
typedef struct SDHCI_HOST {
    UINT64 Base;               /* MMIO 基址 */
    UINT32 Index;              /* 0 = SD0，1 = SD1 */
    UINT32 VendorArea;         /* P_VENDOR_AREA1(0xE8) & 0xFFF，板上应 0x200 */
    UINT16 Specification;      /* HOST_VERSION[7:0] */
    UINT32 ClockHertz;         /* 最近一次请求的时钟 */
    UINT32 ActualClockHertz;   /* 分频后实际时钟（375 MHz 分频，拿不到整数 50 MHz） */
    UINT32 RelativeCardAddress;
    UINT32 BusWidth;           /* 1 / 4 */
    UINT32 CardType;           /* SDHCI_CARD_* */
    UINT32 Response[4];
    UINT32 LastStatus;         /* 出错时的 INT_STATUS */
    UINT32 LastR5;             /* 最近一次 CMD52/53 的 R5 响应字（与 INT_STATUS 分开存） */
    UINT32 LastPresentState;   /* 出错时的 PRESENT_STATE */
    int HighCapacity;          /* SD：SDHC/SDXC */
    int NonRemovable;          /* 1 = 无 CD 脚（wifi-sd） */
    int Ready;
} SDHCI_HOST;

#define SDHCI_BLOCK_SIZE      0x04
#define SDHCI_BLOCK_COUNT     0x06
#define SDHCI_ARGUMENT        0x08
#define SDHCI_TRANSFER_MODE   0x0C
#define SDHCI_COMMAND         0x0E
#define SDHCI_RESPONSE        0x10
#define SDHCI_BUFFER          0x20
#define SDHCI_PRESENT_STATE   0x24
#define SDHCI_HOST_CONTROL    0x28
#define SDHCI_POWER_CONTROL   0x29
#define SDHCI_CLOCK_CONTROL   0x2C
#define SDHCI_TIMEOUT_CONTROL 0x2E
#define SDHCI_SOFTWARE_RESET  0x2F
#define SDHCI_INT_STATUS      0x30
#define SDHCI_INT_ENABLE      0x34
#define SDHCI_SIGNAL_ENABLE   0x38
#define SDHCI_CAPABILITIES    0x40
#define SDHCI_HOST_CONTROL2   0x3E
#define SDHCI_P_VENDOR_AREA1  0xE8
#define SDHCI_HOST_VERSION    0xFE

#define SDHCI_SPEC_300     2u
#define SDHCI_DIV_MASK     0xFFu
#define SDHCI_DIV_MASK_LEN 8u
#define SDHCI_DIV_HI_MASK  0x300u
#define SDHCI_DIVIDER_SHIFT    8u
#define SDHCI_DIVIDER_HI_SHIFT 6u
#define SDHCI_MAX_DIV_300  2046u

#define SDHCI_CMD_INHIBIT    (1u << 0)
#define SDHCI_DATA_INHIBIT   (1u << 1)
/* PRESENT_STATE 的缓冲就绪位：PIO 每个 512 B 块前查一次（Linux sdhci_transfer_pio） */
#define SDHCI_BUF_WR_READY   (1u << 10) /* Buffer Write Enable：空=1 */
#define SDHCI_BUF_RD_READY   (1u << 11) /* Buffer Read Enable：有数据=1 */
#define SDHCI_CARD_INSERTED  (1u << 16)
#define SDHCI_CARD_STABLE    (1u << 17)
#define SDHCI_WIDTH_4BIT     (1u << 1)
/* HOST_CONTROL[2] High Speed Enable：SDIO 协商 HS(50 MHz) 后必须置
 * （Linux sdhci.c:2338-2355 在 MMC_TIMING_SD_HS 时置位）。
 * 注意：这不是电压位，3.3V 由 POWER_CONTROL 决定。 */
#define SDHCI_HIGH_SPEED     (1u << 2)

#define SDHCI_POWER_ON  0x01u
#define SDHCI_POWER_330 0x0Eu

#define SDHCI_CLOCK_INT_EN     (1u << 0)
#define SDHCI_CLOCK_INT_STABLE (1u << 1)
#define SDHCI_CLOCK_CARD_EN    (1u << 2)

#define SDHCI_RESET_ALL  0x01u
#define SDHCI_RESET_CMD  0x02u
#define SDHCI_RESET_DATA 0x04u

#define SDHCI_INT_RESPONSE (1u << 0)
#define SDHCI_INT_DATA_END (1u << 1)
#define SDHCI_INT_SPACE_AVAIL (1u << 4) /* Buffer Write Ready */
#define SDHCI_INT_DATA_AVAIL (1u << 5)  /* Buffer Read Ready */
#define SDHCI_INT_CARD_INT (1u << 8)    /* 卡中断（走 DAT1；本轮不用） */
#define SDHCI_INT_ERROR    (1u << 15)

#define SDHCI_TRNS_BLK_CNT_EN (1u << 1)
#define SDHCI_TRNS_READ       (1u << 4)
#define SDHCI_TRNS_MSBS       (1u << 5) /* 多块 */
/* 写：不置 TRNS_READ */

#define SDHCI_CMD_RESP_NONE       0x00u
#define SDHCI_CMD_RESP_LONG       0x01u
#define SDHCI_CMD_RESP_SHORT      0x02u
#define SDHCI_CMD_RESP_SHORT_BUSY 0x03u
#define SDHCI_CMD_CRC   0x08u
#define SDHCI_CMD_INDEX 0x10u
#define SDHCI_CMD_DATA  0x20u

#define SDHCI_MAKE_CMD(C, F) ((((UINT32)(C) & 0xFFu) << 8) | ((UINT32)(F) & 0xFFu))

/* cv18xx 厂商区偏移（相对 P_VENDOR_AREA1，不是绝对地址） */
#define CV_MSHC_CTRL   0x00u
#define CV_PHY_TX_RX   0x40u
#define CV_PHY_CONFIG  0x4Cu
#define CV_LATENCY_1T  (1u << 1)
#define CV_PHY_TX_BPS  (1u << 0)
/* TX src = invert clk（bits 9:8 = 1），RX src = invert（bits 25:24 = 1），dly = 0 */
#define CV_PHY_DLY_BYPASS ((1u << 8) | (1u << 24))
/*
 * SD1 专用：sdhci-cv181x.c:859-862 在 DS/HS 分支置 MSHC_CTRL[16]
 * （厂商注释 "reg_0x200[16] = 1 for sd1"）。
 * TRM 18.4.4 对同一位的名字是 timer_clk_sel（1 = 32K，0 = 100K），
 * 即卡检测去抖计数器的时钟源；两说并存，按厂商行为置 1。
 */
#define CV_MSHC_CTRL_SD1 (1u << 16)

SDHCI_HOST *SdhciHostAt(UINT32 Index);
int SdhciWifiInitialize(void);
/*
 * shell 的 wifiprobe 入口。Flags：
 *   SDHCI_WIFI_PROBE_FORCE：清掉「SD1 只试一次」的状态后重跑
 *   SDHCI_WIFI_PROBE_WAKE ：顺手把 PE7（dts wakeup-gpio）拉高（一次性实验）
 * 返回 0 = SDIO 枚举走通，-1 = 失败（失败原因已打在日志里）。
 */
#define SDHCI_WIFI_PROBE_FORCE 1u
/* 只 stat 文件、不读内容（评审 W10：原名 read 名不副实，改 stat） */
#define SDHCI_WIFI_FW_STAT 1u
#define SDHCI_WIFI_FW_READ SDHCI_WIFI_FW_STAT /* 旧名保留，等同 stat */
#define SDHCI_WIFI_FW_ID 2u
#define SDHCI_WIFI_FW_WRITE 4u
/* 与 WRITE 同时置位才真写；只给 WRITE 时是"检查模式"，一个字节都不写 */
#define SDHCI_WIFI_FW_CONFIRM 8u
/* B2：patch 表的 dry（只解析打印）/ apply（真写） */
#define SDHCI_WIFI_FW_PATCH_DRY 16u
#define SDHCI_WIFI_FW_PATCH_APPLY 32u
/* S5b：固件阶段（WifiFw.c）。Mode 取上面的位或；只读的 READ/ID 可单独跑 */
int SdhciWifiFw(UINT32 Mode);
/* 只读诊断：读一个 RAM 字（用于真写之后对账） */
int SdhciWifiFwRead(UINT32 Address);
/* 单字写诊断：写完立刻读回（同一命令内），回答"写通不通" */
int SdhciWifiFwWriteDiag(UINT32 Address, UINT32 Value);
/* 发 MM_VERSION_REQ 看有没有 CFM：MM 任务只有下载进去的固件才实现 */
int SdhciWifiFwVersion(void);
/* 显式 FIFO 自检（不参与枚举判定；失败不改 Ready/返回语义） */
int SdhciWifiFifoTest(void);
#define SDHCI_WIFI_PROBE_WAKE 2u
int SdhciWifiProbe(UINT32 Flags);
/*
 * 日志出口切换：Sink == NULL 时直写串口（HalSerialWriteShell，不受 SERIAL_BOOT
 * 通道掩码影响）；shell 里重跑时把它指到 gConsole.Write，输出才和提示符对齐。
 */
void SdhciWifiMarkSink(void (*Sink)(const char *Text));
void BoardSdhciRegister(void);

#endif
