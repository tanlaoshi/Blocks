/*
 * WifiSdioState.c — AIC8800D80 的 SDIO 控制面：唤醒握手、设备软中断应答、状态现场
 *
 * 用户眼里：`wififw join` 之后"只上来 2 个 beacon，然后接收彻底静默"时，这里给出裁决行
 *           `WiFi: sdio: awake=… devirq=…`，并在卡睡着时按厂商的顺序把它叫醒。
 * 从哪读：AicSdioWake（唤醒握手）→ AicSdioIntrAck（设备→主机软中断应答）
 *         → AicSdioIntPeek（CCCR 0x04，只读）→ AicSdioStateDump（十一个寄存器的现场）。
 * 为什么要有这一刀：**接收流控不是"每收 N 帧回一条 DATA_CFM"** —— 厂商 RX 路径
 *   （aicwf_sdio.c:2002-2126、aicsdio.c:1506-1558）在读完 FIFO 之后一个字节都不回写，
 *   读本身就是"确认"。真正需要主机闭环的是**电源/中断状态机**，就三个动作：
 *
 *   1. **设备→主机软中断**：`fn1 0x04`(MISC_INT_STATUS) bit7 置位 ⇒ 读 `fn1 0x01`
 *      (INTR_PENDING)，把 **bit0 清掉**再写回（厂商注释 "dev to host soft irq"）。
 *      不清，固件就一直认为那次通知没人接。
 *      出处：aicwf_sdio.c:2071-2080、aicsdio.c:1514-1524
 *   2. **唤醒**：`fn1 0x02`(INTR_TO_DEVICE) ← **0x11**，然后轮询 `fn1 0x01` 的
 *      **bit4(0x10)**，置起来才算"SDIO 活了"；20 轮 × (10 次 × 200 µs)。
 *      出处：aicwf_sdio.c:943-990（D80 取 0x11）、:2394-2418（func_init 里写 0x11 后
 *      等 5 ms 再验 bit4，失败打 "wakeup fail"）
 *   3. **CCCR 0x04 = 0x07**：D80 打开 fn0/fn1/fn2 的卡中断使能。
 *      出处：aicwf_sdio.c:1630-1635（`sdio_f0_writeb(func, 0x07, 0x04)`）
 *
 * 第 2 条正是"2 帧后静默"最可能的解释：CMD52 读寄存器照样通（所以我们还能打
 * status=00），但**数据通路在睡眠态不给帧**。本文件把这三条补上，并把 bit4/bit0
 * 打进日志 —— 真机上一眼就能判定是不是这个原因。
 */
#include "WifiPrivate.h"

/*
 * 只有这两个地址 WifiPrivate.h 里还没有（其余 0x01/0x03/0x04/0x05/0x07/0x08/0x09
 * 都已定义，直接用，不再起第二个名字）：
 *   aicwf_sdio.h:34-46 的 V3 寄存器表、aicsdio.h:78-80 的 sdio_reg 字段
 */
#define AIC_INTR_ENABLE_REG 0x00u    /* V3：fn1 INTR_ENABLE */
#define AIC_INTR_TO_DEVICE_REG 0x02u /* V3：fn1 INTR_TO_DEVICE（厂商的 wakeup_reg） */
/* 唤醒值 0x11：厂商 aicwf_sdio.c:953-957（D80 分支） */
#define AIC_WAKE_VALUE 0x11u
/* INTR_PENDING 里由协议定义的两个位（aicwf_sdio.c:2071-2080、:966-978） */
#define AIC_PENDING_SOFT_IRQ 0x01u /* bit0：设备→主机软中断，主机负责清 */
#define AIC_PENDING_AWAKE 0x10u    /* bit4：SDIO 已唤醒（厂商判据 `val & 0x10`） */
/* CCCR 0x04（Int Enable）的 fn0/fn1/fn2 三位：aicwf_sdio.c:1631 写 0x07 */
#define AIC_CCCR_INT_ENABLE 0x04u
#define AIC_CCCR_INT_ALL_FUNCS 0x07u
/* 唤醒重试：厂商 20 次写 × (10 次读 × 200 µs)（aicwf_sdio.c:947/970-978），见下 */
#define AIC_WAKE_RETRY 4u       /* 厂商 20 轮；我们的最小延时是 1 ms，4 × 5 × 1 ms 够 */
#define AIC_WAKE_POLL_RETRY 5u
#define AIC_WAKE_POLL_MS 1u

/* 读一个 fn1 寄存器；失败只记一次 R5，由调用方决定怎么说 */
static int AicSdioRead(SDHCI_HOST *Host, UINT32 Reg, UINT8 *Out) {
    if (SdhciIoRead8(Host, 1u, Reg, Out) != 0) {
        AicMarkR5(Host->LastR5);
        return -1;
    }
    return 0;
}

/*
 * CCCR 0x04（Interrupt Enable）**只看不写** —— 与厂商有意不同，理由写在下面的注释里。
 * 厂商写它是和 `sdio_claim_irq()` 成对出现的（aicwf_sdio.c:1626-1635）：Linux 有中断
 * 服务程序，能在"卡拉低 DAT1"之后把它放开。我们是**纯轮询**（没有卡中断服务），
 * 单方面把卡中断打开只会让 DAT1 可能一直压在低电平，而 4-bit 之下 DAT1 就是数据线 1。
 * 所以这一位留给日志：`int_en=` 打出来，谁要试也一眼能看到当前值，但本刀不写。
 */
void AicSdioIntPeek(SDHCI_HOST *Host) {
    UINT8 IntEnable = 0;

    if (SdhciIoRead8(Host, 0u, AIC_CCCR_INT_ENABLE, &IntEnable) != 0) {
        SdhciMarkHex("WiFi: sdio: CCCR 0x04 read failed r5=", Host->LastR5, 8);
        SdhciMark("\n");
        return;
    }
    SdhciMarkHex("WiFi: sdio: cccr int_en=", IntEnable, 2);
    SdhciMark(" (vendor writes 0x07 only together with sdio_claim_irq; we poll, so we leave it)\n");
}

/*
 * 设备→主机软中断应答：fn1 0x04 bit7 置位时，把 fn1 0x01 的 bit0 清掉写回。
 * 保留其它位（尤其 bit4 awake），厂商就是 `int_pending &= ~0x01`（aicwf_sdio.c:2077）。
 * 返回 0 = 有软中断并且已应答；1 = 没有软中断，什么都不用做；-1 = CMD52 失败。
 */
int AicSdioIntrAck(SDHCI_HOST *Host, UINT8 MiscStatus) {
    UINT8 Pending = 0;
    UINT8 Now = 0;

    if ((MiscStatus & (UINT8)AIC_MISC_OTHER_INTR) == 0u) {
        return 1;
    }
    if (AicSdioRead(Host, AIC_INTR_PENDING_REG, &Pending) != 0) {
        SdhciMark("WiFi: sdio: dev irq ack read 0x01 failed\n");
        return -1;
    }
    if (SdhciIoWrite8(Host, 1u, AIC_INTR_PENDING_REG,
                      (UINT8)(Pending & (UINT8)~AIC_PENDING_SOFT_IRQ)) != 0) {
        SdhciMarkHex("WiFi: sdio: dev irq ack write failed r5=", Host->LastR5, 8);
        SdhciMark("\n");
        return -1;
    }
    (void)SdhciIoRead8(Host, 1u, AIC_INTR_PENDING_REG, &Now);
    SdhciMarkHex("WiFi: sdio: dev irq ack pend=", Pending, 2);
    SdhciMarkHex(" now=", Now, 2);
    SdhciMark(" (misc bit7 was set)\n");
    return 0;
}

/* "叫不醒"这条只报一次：轮询里每秒钟都会走到，刷屏反而看不见别的 */
static UINT32 sAicWakeWarned;

/*
 * 唤醒握手：读 fn1 0x01，bit4 已是 1 就直接返回（**一个字节都不写**）。
 * 否则 fn1 0x02 ← 0x11，然后等 bit4；最多 4 轮写、每轮 5 次 × 1 ms（共 ~20 ms）。
 * 判据行（原样）：
 *   `WiFi: sdio wake: fn1 0x02<-11 wrote=N pend=1x (awake)`
 *   `WiFi: sdio wake: STILL ASLEEP after writes=04 pend=0x (...)`（只打一次）
 */
int AicSdioWake(SDHCI_HOST *Host) {
    UINT8 Pending = 0;
    UINT32 Write;

    if (AicSdioRead(Host, AIC_INTR_PENDING_REG, &Pending) != 0) {
        SdhciMark("WiFi: sdio wake: read fn1 0x01 failed\n");
        return -1;
    }
    if ((Pending & (UINT8)AIC_PENDING_AWAKE) != 0u) {
        return 0; /* 醒着，什么都不写 */
    }
    for (Write = 0; Write < AIC_WAKE_RETRY; Write++) {
        UINT32 Poll;

        if (SdhciIoWrite8(Host, 1u, AIC_INTR_TO_DEVICE_REG, (UINT8)AIC_WAKE_VALUE) != 0) {
            SdhciMarkHex("WiFi: sdio wake: write fn1 0x02 failed r5=", Host->LastR5, 8);
            SdhciMark("\n");
            return -1;
        }
        for (Poll = 0; Poll < AIC_WAKE_POLL_RETRY; Poll++) {
            if (AicSdioRead(Host, AIC_INTR_PENDING_REG, &Pending) != 0) {
                return -1;
            }
            if ((Pending & (UINT8)AIC_PENDING_AWAKE) != 0u) {
                SdhciMarkHex("WiFi: sdio wake: fn1 0x02<-11 wrote=", Write + 1u, 2);
                SdhciMarkHex(" pend=", Pending, 2);
                SdhciMark(" (awake)\n");
                return 0;
            }
            SdhciDelayMilliseconds(AIC_WAKE_POLL_MS);
        }
    }
    if (sAicWakeWarned == 0u) {
        sAicWakeWarned = 1u;
        SdhciMarkHex("WiFi: sdio wake: STILL ASLEEP after writes=", AIC_WAKE_RETRY, 2);
        SdhciMarkHex(" pend=", Pending, 2);
        SdhciMark(" (fn1 0x01 bit4=0: data path may stay asleep -> RX silent;"
                  " vendor aicwf_sdio.c:2394-2418 expects bit4)\n");
    }
    return -1;
}

/*
 * 现场印刷：十一个 fn1 寄存器 + CCCR 三位 + 主机侧状态，最后一行是裁决。
 * 只在"该有数据却没有"的路径上调（AicWaitMessagePending 超时），不刷屏。
 */
void AicSdioStateDump(SDHCI_HOST *Host) {
    UINT8 IntEnable = 0;
    UINT8 Pending = 0;
    UINT8 ToDevice = 0;
    UINT8 FlowQ1 = 0;
    UINT8 Misc = 0;
    UINT8 ByteLength = 0;
    UINT8 ByteEnable = 0;
    UINT8 MiscCtrl = 0;
    UINT8 FlowQ2 = 0;
    UINT8 IoEx = 0;
    UINT8 IoRx = 0;
    UINT8 CccrInt = 0;

    (void)SdhciIoRead8(Host, 1u, AIC_INTR_ENABLE_REG, &IntEnable);
    (void)SdhciIoRead8(Host, 1u, AIC_INTR_PENDING_REG, &Pending);
    (void)SdhciIoRead8(Host, 1u, AIC_INTR_TO_DEVICE_REG, &ToDevice);
    (void)SdhciIoRead8(Host, 1u, AIC_FLOW_CTRL_REG_V3, &FlowQ1);
    (void)SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Misc);
    (void)SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &ByteLength);
    (void)SdhciIoRead8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, &ByteEnable);
    (void)SdhciIoRead8(Host, 1u, AIC_MISC_CTRL_REG, &MiscCtrl);
    (void)SdhciIoRead8(Host, 1u, AIC_FLOW_CTRL_Q2_REG, &FlowQ2);
    (void)SdhciIoRead8(Host, 0u, 0x02u, &IoEx);
    (void)SdhciIoRead8(Host, 0u, 0x03u, &IoRx);
    (void)SdhciIoRead8(Host, 0u, AIC_CCCR_INT_ENABLE, &CccrInt);

    SdhciMark("WiFi: sdio state: fn1 00=");
    SdhciMarkHex("", IntEnable, 2);
    SdhciMarkHex(" 01=", Pending, 2);
    SdhciMarkHex(" 02=", ToDevice, 2);
    SdhciMarkHex(" 03=", FlowQ1, 2);
    SdhciMarkHex(" 04=", Misc, 2);
    SdhciMarkHex(" 05=", ByteLength, 2);
    SdhciMarkHex(" 07=", ByteEnable, 2);
    SdhciMark("\n");
    SdhciMark("WiFi: sdio state: fn1 08=");
    SdhciMarkHex("", MiscCtrl, 2);
    SdhciMarkHex(" 09=", FlowQ2, 2);
    SdhciMarkHex(" cccr ioex=", IoEx, 2);
    SdhciMarkHex(" iorx=", IoRx, 2);
    SdhciMarkHex(" int_en=", CccrInt, 2);
    SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
    SdhciMark("\n");
    SdhciMark("WiFi: sdio verdict: awake=");
    SdhciMarkDecimal("", (UINT32)((Pending & (UINT8)AIC_PENDING_AWAKE) != 0u));
    SdhciMark(" devirq=");
    SdhciMarkDecimal("", (UINT32)((Pending & (UINT8)AIC_PENDING_SOFT_IRQ) != 0u));
    SdhciMark(" blocks=");
    SdhciMarkDecimal("", (UINT32)(Misc & 0x7Fu));
    if ((Misc & (UINT8)AIC_MISC_OTHER_INTR) != 0u) {
        SdhciMark(" (misc bit7: device soft irq pending, host must clear fn1 0x01 bit0)");
    }
    if ((Pending & (UINT8)AIC_PENDING_AWAKE) == 0u) {
        SdhciMark(" (bit4=0: card asleep -> RX silence; wake = fn1 0x02<-0x11)");
    }
    SdhciMark("\n");
}
