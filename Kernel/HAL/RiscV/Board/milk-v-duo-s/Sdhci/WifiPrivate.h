/*
 * WifiPrivate.h — AIC8800D80 固件下载的跨文件内部共享声明
 *
 * **扁平目录的例外**：Sdhci/ 下每个 .c 都是 SDHCI 层的一个单元，本头文件只放
 * Wifi*.c 之间共享的宏/类型/extern/原型，不放进 Sdhci/Private.h（那是 SDHCI 层公共头，
 * 混一起就又会"改一条动一片"）。
 */
#ifndef SDHCI_WIFI_PRIVATE_H
#define SDHCI_WIFI_PRIVATE_H

#include "Sdhci/Private.h"
#include "FileSystem.h"

/* ---- AIC 消息常量（aic_bsp_driver.h:144-215、aicsdio.h:35-64） ---- */
#define AIC_TASK_MM 0u
#define AIC_TASK_DBG 1u
/* MM_VERSION_REQ = LMAC_FIRST_MSG(TASK_MM)+4，CFM 紧随其后（lmac_msg.h:136-150） */
#define AIC_MM_RESET_REQ 0x0000u
#define AIC_MM_RESET_CFM 0x0001u
#define AIC_MM_VERSION_REQ 0x0004u
#define AIC_MM_VERSION_CFM 0x0005u
/*
 * PR-31：配置无线电的三条（lmac_msg.h:136-146 的 mm_msg_tag，MM_START 是第 2 项；
 * :1667-1674 的 me_msg_tag，前两条；LMAC_FIRST_MSG(task)=task<<10，TASK_ME=5）。
 */
#define AIC_TASK_ME 5u
#define AIC_MM_START_REQ 0x0002u
#define AIC_MM_START_CFM 0x0003u
#define AIC_ME_CONFIG_REQ 0x1400u
#define AIC_ME_CONFIG_CFM 0x1401u
#define AIC_ME_CHAN_CONFIG_REQ 0x1402u
#define AIC_ME_CHAN_CONFIG_CFM 0x1403u
/*
 * 参数长度 = 厂商 sizeof(struct ...)，逐字段核对过（报告 §1 的偏移表）。
 * mm_start 特别说明：字段和只有 70 B，但结构体尾对齐到 4 ⇒ sizeof = 72，
 * 厂商 rwnx_msg_zalloc(..., sizeof(struct mm_start_req)) 发的就是 72。
 */
#define AIC_ME_CONFIG_LENGTH 112u
#define AIC_ME_CHAN_CONFIG_LENGTH 254u
#define AIC_MM_START_LENGTH 72u
#define AIC_BRINGUP_MAX_LENGTH AIC_ME_CHAN_CONFIG_LENGTH
/* bringup 每步的 CFM 预算：4 × AIC_CFM_TIMEOUT_MS(500) = 2000 ms */
#define AIC_BRINGUP_CFM_TRIES 4u
/* dry 只打前 64 B（覆盖 ht_cap 32 B + vht_cap 12 B + he_cap 前 20 B，够看填错没有） */
#define AIC_BRINGUP_DUMP_BYTES 64u
#define AIC_DRV_TASK_ID 100u
#define AIC_MSGID(Task, Index) ((UINT16)(((UINT16)(Task) << 10) | (UINT16)(Index)))
#define AIC_DBG_MEM_READ_REQ AIC_MSGID(AIC_TASK_DBG, 0u)
#define AIC_DBG_MEM_READ_CFM AIC_MSGID(AIC_TASK_DBG, 1u)
#define AIC_DBG_MEM_BLOCK_WRITE_REQ AIC_MSGID(AIC_TASK_DBG, 11u)
#define AIC_DBG_MEM_BLOCK_WRITE_CFM AIC_MSGID(AIC_TASK_DBG, 12u)
#define AIC_DBG_START_APP_REQ AIC_MSGID(AIC_TASK_DBG, 13u)  /* 0x40D */
#define AIC_DBG_START_APP_CFM AIC_MSGID(AIC_TASK_DBG, 14u)  /* 0x40E */

#define AIC_TYPE_CFG_CMD_RSP 0x11u
#define AIC_TYPE_CFG_DATA_CFM 0x12u
#define AIC_TYPE_CFG_PRINT 0x13u
#define AIC_MISC_OTHER_INTR 0x80u /* fn1 0x04 bit7：芯片有消息给主机 */

#define AIC_RD_FIFO_REG 0x0Fu  /* V3：fn2 读 FIFO */
#define AIC_WR_FIFO_REG 0x10u  /* V3：fn1 写 FIFO */
#define AIC_MISC_INT_REG 0x04u /* V3：fn1 MISC_INT_STATUS */
#define AIC_INTR_PENDING_REG 0x01u /* V3：fn1 INTR_PENDING */
#define AIC_MISC_CTRL_REG 0x08u    /* V3：fn1 MISC_CTRL */
#define AIC_FLOW_CTRL_Q2_REG 0x09u /* V3：fn1 FLOW_CTRL_Q2 */

/* 帧长上限：16 B 头 + 参数；FIFO 一块 512 B，写时分块 */
#define AIC_MSG_MAX 1600u
/*
 * e2a 消息的"有效性戳"（aic_bsp_driver.h:62-71 struct ipc_e2a_msg 的 pattern 字段）。
 * 厂商拿它判断一个 MSG buffer 里是不是真有一条消息；我们也拿它当硬判据，
 * 比 `hdr[2]&0x7f == 0x11` 强得多。
 *
 * 帧布局（实测 20 B 一帧，plen=4）：
 *   +0  4 B 块头： len(12bit)=总长-4 | type | crc8
 *   +4  4 B e2a：  id(u16) | dummy_dest(u16)
 *   +8  4 B e2a：  dummy_src(u16) | param_len(u16)
 *   +12 4 B       pattern = 0xaddede2a
 *   +16 params    ← 以前从 +12 拷，整体偏移 4 B，把 pattern 当成了 param[0]
 * 即 总长 = 16 + param_len，块头 len 字段 = 12 + param_len。
 */
#define AIC_E2A_PATTERN 0xADDEDE2Au
#define AIC_E2A_PARAM_OFF 16u
#define AIC_FIFO_CHUNK 512u
/* 厂商 aicwf_sdio.h:28/51-53：流控队列、tx 缓冲、尾补齐 */
#define AIC_FLOW_CTRL_REG_V3 0x03u
#define AIC_TX_BUFFER_SIZE 1536u
#define AIC_FC_RETRY_COUNT 50u
#define AIC_BLK_WRITE 1024u /* 厂商一块 1 KB */
#define AIC_CFM_TIMEOUT_MS 500u
#define AIC_CFM_POLL_MS 1u

/* D80 u02 的固件落点（aic_bsp_driver.h:358-359） */
#define AIC_D80_U02_ADID_ADDR 0x00201940u
/* 兜底值，正常路径不用它：表里的 addr_patch=0x001E0000 才是真的（W3） */
#define AIC_D80_U02_PATCH_ADDR 0x0020B43Cu
/* fmacfw 长度：越界检查用（patch 写绝不允许落到这片主固件区） */
#define AIC_D80_FMAC_SIZE 331348u
#define AIC_D80_CHIP_REV_ADDR 0x40500000u /* 版本号在芯片存储区，不是寄存器 */

/* 厂商 sdio_reg 里我们还要用到的两个（aicsdio.h:44、aicsdio.c:1714-1719） */
#define AIC_BYTEMODE_ENABLE_REG 0x07u /* fn1 V3：0x01 = 允许字节模式（aicsdio.c:1862） */
#define AIC_BYTEMODE_LEN_REG 0x05u    /* fn1 V3：字节模式长度（低 8 位，单位 4 B） */
#define AIC_BYTEMODE_LEN_MSB_REG 0x06u
#define AIC_FBR_BLKSIZE_L 0x110u
#define AIC_FBR_BLKSIZE_H 0x111u

#define AIC_FW_DIR "/Assets/Fw/aic8800/"
#define AIC_FW_ADID AIC_FW_DIR "fw_adid.bin"
#define AIC_FW_PATCH AIC_FW_DIR "fw_patch.bin"
#define AIC_FW_PATCH_TABLE AIC_FW_DIR "fw_patch_table.bin"
#define AIC_FW_FMAC AIC_FW_DIR "fmacfw.bin"

/* 下载落点（aic_bsp_driver.h:345、358-359） */
#define AIC_RAM_FMAC_FW_ADDR 0x00120000u
#define AIC_START_APP_AUTO 1u /* aic_bsp_driver.h:246 */

/* patch 表（fw_patch_table.bin）的格式与类型，照 aic_bsp_driver.h:361、:425-433 */
#define AICBT_PT_TAG "AICBT_PT_TAG" /* 16 B，后补 0 */
#define AICBT_PT_INF 0x00u          /* 地址信息元组 */
#define AICBT_PT_TRAP 0x01u
#define AICBT_PT_B4 0x02u
#define AICBT_PT_BTMODE 0x03u
#define AICBT_PT_PWRON 0x04u
#define AICBT_PT_AF 0x05u
#define AICBT_PT_VER 0x06u
#define AICBT_PT_NAME 16u
#define AICBT_PT_HDR 16u

/* DBG_MEM_WRITE_REQ/CFM：单字32 位写（aic_bsp_driver.h:187-188，REQ=0x402 CFM=0x403） */
#define AIC_DBG_MEM_WRITE_REQ AIC_MSGID(AIC_TASK_DBG, 2u)
#define AIC_DBG_MEM_WRITE_CFM AIC_MSGID(AIC_TASK_DBG, 3u)

/* 分块大小：**不一次性把 331 KB 读进内核堆**，按 8 KB 复用同一缓冲 */
#define AIC_FW_CHUNK 8192u
#define AIC_FW_STAGE_MAX 5u
/*
 * 阶段表（顺序照 aic8800_bsp/aic_bsp_main.c:23-33 的 fw_u02 表 +
 * aic_bsp_driver.c:1417-1428 的上传顺序 + :1774-1790 的启动顺序）：
 *
 *   adid    → FW_RAM_ADID_BASE_ADDR_8800D80_U02
 *   patch   → FW_RAM_PATCH_BASE_ADDR_8800D80_U02   （bt_patch = fw_patch.bin）
 *   fmacfw  → RAM_FMAC_FW_ADDR = 0x00120000
 *   start   → DBG_START_APP_REQ{bootaddr, boottype=1}
 *
 * 注意：厂商**不**把 fw_patch_table.bin 原样上传 —— 它是主机侧数据，由
 * `aicbt_patch_table_alloc()` 解析、再由 `aicwifi_patch_config_8800d80()`
 * 把表项写进 RAM（aic_bsp_driver.c:1517-1530、aic8800d80_compat.c:114+）。
 * 那一步本版**未实现**，写盘/启动都成功但固件起不来的话，下一个门就是它。
 */
/* 阶段类型：文件 / patch 表落地 / start_app */
#define AIC_STAGE_FILE 0u
#define AIC_STAGE_PATCHTBL 1u
#define AIC_STAGE_START 2u

typedef struct {
    const char *Name;
    const char *Path;
    UINT32 Address;
    UINT32 ExpectSize;
    int Required;
    UINT8 Kind;
} AIC_FW_STAGE;
/* patch 表里的地址信息元组（aic_bsp_driver.h:541-551 的 aicbt_patch_info_t） */
typedef struct {
    UINT32 InfoLength;
    UINT32 AdidAddressInfo;
    UINT32 AddressAdid;
    UINT32 PatchAddressInfo;
    UINT32 AddressPatch;
    UINT32 ResetAddress;
    UINT32 ResetValue;
    UINT32 AdidFlagAddress;
    UINT32 AdidFlag;
} AIC_PATCH_INFO;
/* 计划表：把"要写哪些字"先全部算出来（先算后写） */
#define AIC_PLAN_MAX 160
#define AIC_PLAN_MEM 0u   /* 普通写 */
#define AIC_PLAN_CFG 1u   /* 厂商会用运行时配置覆盖的写（BTMODE） */
#define AIC_PLAN_DELAY 2u /* PWRON 之后的 udelay(500) */

typedef struct {
    UINT8 Kind;  /* mem / cfg */
    UINT8 Type;  /* AICBT_PT_* */
    UINT32 Address;
    UINT32 Value;  /* 实际要写的值（MODE 已被厂商运行时值覆盖） */
    UINT32 TableValue;  /* 表里原本的值，只用于对账打印 */
} AIC_PLAN_ITEM;
/* D80 的 BT 默认值（aic_bsp_driver.h:505-517；CONFIG_SDIO_BT=n → btport=UART） */
#define AIC_BT_HWINFO 0xFFFFFFFFu /* AICBSP_HWINFO_DEFAULT (-1)：未知 */
#define AIC_BT_CPMODE 0u          /* AICBSP_CPMODE_WORK */
#define AIC_BT_BTMODE 5u          /* AICBT_BTMODE_BT_ONLY_COANT */
#define AIC_BT_BTPORT 2u          /* AICBT_BTPORT_UART */
#define AIC_BT_BAUD 1500000u      /* AICBT_UART_BAUD_1_5M */
#define AIC_BT_FLOWCTRL 1u        /* AICBT_UART_FLOWCTRL_ENABLE */
#define AIC_BT_LPM 0u
#define AIC_BT_TXPWR 0x00006F2Fu /* AICBT_TXPWR_LVL_8800d80 */
/*
 * patch_config_8800d80（aic8800d80_compat.c:114-248）。
 * 评审 P1-2：4 个读是"最可能失败"的一步，必须提到 100 条写之前做纯读 preflight，
 * 读不到/不合法就 0 写退出；评审 P1-3：合法窗口按镜像实际范围写死，不再只看 0/0xffffffff。
 */
typedef struct {
    UINT32 ConfigBase;
    UINT32 StrBase;
    UINT32 Version;
    UINT32 PatchAddress;
    UINT32 PairCount;
} AIC_PATCH_CFG;

/* 合法窗口：fmacfw 0x120000..0x170E54、patch 0x1E0000..0x1E7558、adid 0x201940..0x201FEC
   全部落在 [0x00010000, 0x00210000) 内；并要求 4 字节对齐。 */
#define AIC_RAM_LO 0x00010000u
#define AIC_RAM_HI 0x00210000u
/* 本次运行是否真的把 patch 应用过（start_app 的前置条件） */
/* 评审 P1-4：三态，避免"失败后重放"和"掉电后仍以为已应用" */
#define AIC_PATCH_NONE 0u
#define AIC_PATCH_DONE 1u
#define AIC_PATCH_POISONED 2u
/* ---------- B2：dry 清单 + apply ---------- */

#define AIC_PATCH_NOT_IMPLEMENTED (-2)

/* ---- 全局状态（定义在 WifiFw.c，其余文件只读这里） ---- */
extern UINT8 gAicMessage[AIC_MSG_MAX];
extern UINT8 gAicRx[512];
extern UINT8 gAicFwBuffer[AIC_FW_CHUNK];
extern AIC_FW_STAGE gAicStages[AIC_FW_STAGE_MAX];
extern UINT32 gWifiFwState;
extern volatile UINT32 gAicPatchReady;
extern UINT32 gAicPatchState;
extern UINT32 gWifiFwStageIndex;
extern UINT32 gWifiFwSent;
extern AIC_PLAN_ITEM gAicPlan[AIC_PLAN_MAX];
extern UINT32 gAicModeValue[9];

/* ---- 跨文件函数原型 ---- */
UINT16 AicGet16(const UINT8 *At);
const char *AicPatchTypeName(UINT32 Type);
int AicFifoRead(SDHCI_HOST *Host, UINT32 Fn, UINT8 *Data, UINT32 Length);
int AicFwCheckStage(AIC_FW_STAGE *Stage, UINT32 *OutSize);
int AicFwLoadPatchTable(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 BufferSize, UINT32 *OutLength);
int AicFwPushStage(SDHCI_HOST *Host, AIC_FW_STAGE *Stage, UINT32 Size);
int AicFwReadChunk(const char *Path, UINT32 Offset, UINT8 *Buffer, UINT32 Want, UINT32 *OutGot);
int AicFwStartApp(SDHCI_HOST *Host, UINT32 BootAddress);
int AicMemBlockWrite(SDHCI_HOST *Host, UINT32 Address, UINT32 Size, const UINT8 *Data);
int AicMemRead(SDHCI_HOST *Host, UINT32 Address, UINT32 *Value);
int AicMemWrite32(SDHCI_HOST *Host, UINT32 Address, UINT32 Value);
int AicPatchApply(SDHCI_HOST *Host, const UINT8 *Table, UINT32 Length);
int AicPatchBuildPlan(const UINT8 *Table, UINT32 Length, UINT32 *OutCount);
int AicPatchDry(void);
int AicPatchSelfCheck(void);
int AicPatchTableParse(const UINT8 *Buffer, UINT32 Length, AIC_PATCH_INFO *Info);
int AicRecvMessage(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam, UINT32 *OutParamLength);
int AicRecvMessageTimeout(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam, UINT32 *OutParamLength, UINT32 Tries, UINT32 TimeoutMs);
/* 变长消息（SCANU_RESULT_IND）用：把整帧原始字节交给调用方，不只拷 param */
int AicReceiveFrameTimeout(SDHCI_HOST *Host, UINT8 *Frame, UINT32 MaxFrame, UINT16 *OutId, UINT32 *OutFrameLength, UINT32 *OutParamLength, UINT32 Tries, UINT32 TimeoutMs);
int AicSendMessage(SDHCI_HOST *Host, UINT16 Id, const void *Param, UINT16 ParamLength);
int AicSendMessageEx(SDHCI_HOST *Host, UINT16 Id, UINT16 DestId, const void *Param, UINT16 ParamLength);
int AicWaitMessagePending(SDHCI_HOST *Host, UINT32 TimeoutMs, UINT32 *OutFn, UINT32 *OutLength);
int AicWifiVendorPrepare(SDHCI_HOST *Host);
/* PR-31：三条配置消息的拼装（WifiConfigMessage.c）。Buffer 必须 4 B 对齐 */
int AicBringupLayoutCheck(void);
UINT32 AicChanConfigBuild(UINT8 *Buffer);
UINT32 AicMeConfigBuild(UINT8 *Buffer);
UINT32 AicMmStartBuild(UINT8 *Buffer);
void AicDumpBytes(const UINT8 *Data, UINT32 Length);
void AicFwFail(const char *Stage, UINT32 Sent, const char *What);
void AicFwWaitBootMessage(SDHCI_HOST *Host);
void AicMarkR5(UINT32 R5);
void AicMarkXferParams(SDHCI_HOST *Host, const char *Tag, UINT32 Bytes, UINT32 Blocks);
void AicPatchPrintPlan(UINT32 Count);
void AicPut16(UINT8 *At, UINT16 Value);

#endif
