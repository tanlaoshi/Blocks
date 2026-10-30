/*
 * SdioSelfTest.c — SDIO 自证测试：CIS 读比对、DAT 线读校验、FIFO 1536 B 往返
 *
 * 职责边界：只回答"这条通道真的能搬数据吗"——都是可独立调用的自检查询；
 *           正常枚举路径只在 `wififw fifotest` 时才走到这里。
 * 从 Sdio.c 原样搬出（纯搬家，函数体逐字不动），行为零变化。
 */
#include "Sdhci/Private.h"


/* 自检缓冲：1536 B = 3 × 512（只有 self-test 用到，定义放这里） */
UINT8 gSdioPattern[SDIO_SELFTEST_BYTES];
UINT8 gSdioReadback[SDIO_SELFTEST_BYTES];

/*
 * DAT 读方向的"确定有货"验证：用 CMD53 字节模式从 fn0 的 Common CIS（0x1000）
 * 读 16 B，再和 CMD52 逐字节读的结果比对。
 * CIS 是卡自己的 ROM，一定回数据 —— 与模块固件在不在、FIFO 里有没有消息无关，
 * 所以它能单独证明 DAT0-3 的读方向没问题。
 */
/*
 * 把"CMD53 读回来的一串字节"和"CMD52 逐字节读的同一地址"并排打出来并比对。
 * 两个来源不同：CMD53 走数据线（DAT0-3）批量读，CMD52 走命令线单字节读。
 * 两者一致 → 数据线读方向可信；不一致 → 直接把两边原文摆出来，不用猜。
 */
static int SdioCmpCisRead(SDHCI_HOST *Host, const char *Tag, UINT32 Address, UINT8 *Buf,
                          UINT32 Bytes) {
    UINT8 Ref[16];
    UINT32 i;
    UINT32 Bad = Bytes;

    if (Bytes > 16u) {
        Bytes = 16u;
    }

    SdhciMark(Tag);
    SdhciMarkHex(" cmd53@", Address, 6);
    SdhciMark(":");
    for (i = 0; i < Bytes; i++) {
        SdhciMarkHex(" ", Buf[i], 2);
    }
    SdhciMark("\n");
    SdhciMark("WiFi:  cmd52 ref            :");
    for (i = 0; i < Bytes; i++) {
        if (SdhciIoRead8(Host, 0, Address + i, &Ref[i]) != 0) {
            SdhciMark(" <read fail>");
            break;
        }
        SdhciMarkHex(" ", Ref[i], 2);
    }
    SdhciMark("\n");
    for (i = 0; i < Bytes; i++) {
        if (Buf[i] != Ref[i]) {
            Bad = i;
            break;
        }
    }
    if (Bad == Bytes) {
        return 0;
    }
    SdhciMarkHex("WiFi:  mismatch +", Bad, 2);
    SdhciMarkHex(" cmd53=", Buf[Bad], 2);
    SdhciMarkHex(" cmd52=", Ref[Bad], 2);
    SdhciMark("\n");
    return -1;
}

/*
 * DAT 读方向自证：CMD53（字节模式、**地址自增**）从 fn0 的 Common CIS 读 16 B，
 * 与 CMD52 逐字节读的结果比对。
 *
 * 09:23 真机上的 `DAT read mismatch at 01 got=20 want=04` 就是这里的 bug：
 * 当时 Increment 传的是 0 → OP_CODE=0 = **固定地址**，卡老老实实把 0x1000 的
 * 那个 0x20 重复回了 16 次，所以 buf[1] 还是 0x20，而 CIS 的真值是 0x04。
 * 读路径本身没错，是参数错了（`20 04 a1 c8` 里 0x20 恰好是 MANFID tuple 头，
 * 看着像"读到了正确内容"，特别容易骗过人）。
 *
 * 失败时退 25 MHz / 1-bit 再对照一次（用户要求第 4 点），然后恢复总线。
 */
static int SdioDatReadCheck(SDHCI_HOST *Host, UINT8 HostControl, UINT32 RestoreHertz) {
    UINT32 i;
    UINT8 Buf[16];
    UINT8 Low[16];
    int Rc;

    for (i = 0; i < 16u; i++) {
        Buf[i] = 0;
    }
    /* Increment=1：CIS 是存储区，必须自增；固定地址只适合 FIFO */
    if (SdhciIoRwExtended(Host, 0, 0x1000u, 0, 1, 0, 16u, Buf) != 0) {
        SdioMarkXfer(Host, "WiFi: dat rd cmd53 fn0 cis incr=1 FAIL");
        return -1;
    }
    Rc = SdioCmpCisRead(Host, "WiFi: dat rd", 0x1000u, Buf, 16u);
    if (Rc == 0) {
        SdhciMark("WiFi: DAT read ok (cmd53 incr=1 == cmd52, 16B)\n");
        return 0;
    }

    /* 降速对照：25 MHz / 1-bit（卡侧仍是 4-bit，CMD53 只走 DAT0 时无影响） */
    SdhciMark("WiFi: dat rd retry 25MHz 1-bit\n");
    SdhciWrite8(Host, SDHCI_HOST_CONTROL, 0);
    (void)SdhciClockConfigure(Host, SD_TRANSFER_HERTZ);
    for (i = 0; i < 16u; i++) {
        Low[i] = 0;
    }
    if (SdhciIoRwExtended(Host, 0, 0x1000u, 0, 1, 0, 16u, Low) == 0) {
        Rc = SdioCmpCisRead(Host, "WiFi: low cis0", 0x1000u, Low, 16u);
        SdhciMark(Rc == 0 ? "WiFi: low-speed read matches cmd52\n"
                          : "WiFi: low-speed read ALSO mismatches\n");
    } else {
        SdhciMark("WiFi: low-speed cmd53 failed\n");
        Rc = -1;
    }
    SdhciWrite8(Host, SDHCI_HOST_CONTROL, HostControl);
    (void)SdhciClockConfigure(Host, RestoreHertz);
    SdhciMarkHex("WiFi: bus restored hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
    SdhciMarkHex(" clk=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
    SdhciMark("\n");
    return Rc;
}

/*
 * DAT 方向自检（S3 收尾）。
 *
 * 顺序按"能确定的事先做"：
 *   1) CMD53 fn0 CIS 读 16 B   —— 证明 DAT0-3 读方向通（与固件无关）
 *   2) FIFO 写 1536 B          —— 证明写方向通（S5 下载固件要用）
 *   3) FIFO 读"探一下"         —— **只读 4 B，且允许失败**
 *
 * 第 3 步为什么不再读 1536 B：AIC 的 fn1 FIFO 是**消息通道**，不是回环。
 * 厂商只在芯片有消息时才读（aicwf_sdio_bus_rx → sdio_readsb，且由 flow-ctrl/中断
 * 驱动），FIFO 空的时候去读，DAT 线永远不结束 —— 09:10 那次
 * `pst=03f20206`（DOING_READ + DAT_ACTIVE + DATA_INHIBIT）就是这个。
 * 所以这里只在"有消息"时才有意义；没有就是正常，收尾把总线拉回来即可。
 */
int SdioFifoTest(SDHCI_HOST *Host, int MultiBlock) {
    UINT32 i;
    UINT8 HostControl = SdhciRead8(Host, SDHCI_HOST_CONTROL);
    UINT32 RestoreHertz = Host->ActualClockHertz;

    /* fn1 的两个 FIFO 窗口是固定常量（aicsdio.c:1708-1719 按 chipid 选），
       不需要先写什么 window/len 寄存器（用户要求第 2 点）。 */
    SdhciMarkHex("WiFi: fn1 fifo rd=", AIC_RD_FIFO_ADDR_V3, 2);
    SdhciMarkHex(" wr=", AIC_WR_FIFO_ADDR_V3, 2);
    SdhciMark(" (fixed, no window reg)\n");

    if (SdioDatReadCheck(Host, HostControl, RestoreHertz) != 0) {
        (void)SdioRecoverBus(Host, HostControl, RestoreHertz);
    }

    for (i = 0; i < SDIO_SELFTEST_BYTES; i++) {
        gSdioPattern[i] = (UINT8)((i * 7u + 0xA5u) & 0xFFu);
        gSdioReadback[i] = 0;
    }
    SdhciMark(MultiBlock ? "WiFi: FIFO mode=block x3\n" : "WiFi: FIFO mode=byte x3\n");
    if (SdioFifoTransfer(Host, 1, gSdioPattern, MultiBlock) != 0) {
        SdhciFail(Host, "WiFi: FIFO write 1536B fail");
        (void)SdioRecoverBus(Host, HostControl, RestoreHertz);
        return -1;
    }
    SdhciMark("WiFi: FIFO write 1536B ok\n");

    /*
     * 评审 P1-1：这里**不再盲读 4 B**。芯片有没有消息只能由 fn1 0x04 的状态决定
     * （aicsdio.c:1505-1545），盲读空 FIFO 会把 DAT 线挂住，污染后面的 wififw id。
     * 自检只验"写得进去 + 总线还活着"，然后强制断电收尾。
     */
    SdhciMark("WiFi: FIFO read skipped (poll fn1 0x04 instead, no blind read)\n");
    (void)SdioRecoverBus(Host, HostControl, RestoreHertz);
    SdhciMarkHex("WiFi: fifo self-test end, bus restored hctl=",
                 SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
    SdhciMark("\n");
    return 0;
}
