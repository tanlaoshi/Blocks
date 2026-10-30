/*
 * Sdio.c — Duo S SD1（wifi-sd）SDIO 枚举：对外入口与顺序表主函数
 *
 * 本文件只放"从插卡到可用"的对外入口与主流程：
 *   SdhciWifiProbe / SdhciWifiFifoTest / SdhciWifiReady / SdhciWifiChipIsD80 /
 *   SdhciWifiBusRecover / SdhciWifiInitialize
 *
 * SdhciWifiInitialize 已拆成「顺序表主函数 + 每步一个业务函数」：
 *   主函数（本文件末尾，36 行）只按顺序调用，不含寄存器/延时/循环/打印；
 *   ProbePrepare / ControllerBringUp / ModulePowerOn / ProbeFinish → SdioInit.c
 *   CardEnumerate / CccrConfigure / SpeedSwitch / BusWidthSwitch / FunctionEnable
 *                                                               → SdioTransfer.c
 *   CisVerify / CisReport                                       → SdioCis.c
 * 步骤间上下文 WIFI_PROBE 定义在 Private.h（多文件共用）。
 * 其余按职责拆到同目录：
 *   SdioInit.c     probe 生命周期 + 控制器/电源起步
 *   SdioCis.c      卡是谁（CIS 解析、身份判定、fn1 现场）
 *   SdioTransfer.c 枚举/总线配置、CMD53 收发、错误位、总线恢复
 *   SdioSelfTest.c 通道能不能用（自检查询）
 *
 * 时序全部对照 Archive/DUO-WiFi-SDIO时序.md §2 与厂商驱动。
 *
 * 行数说明：上一刀本文件 609 行、SdhciWifiInitialize 单函数 494 行（115-608）；
 * 本刀拆完后本文件 162 行，主函数 36 行，各步骤函数均 ≤80 行。
 */
#include "Sdhci/Private.h"

int gWifiSdioState; /* 0 = 未试；1 = 成功；-1 = 失败（只报一次，不重试刷屏） */
/* S5b：CIS 里读到的 vendor/device，厂商 aicwf_sdio_chipmatch 就是拿它们认芯片的 */
UINT16 gWifiChipVendor;
UINT16 gWifiChipDevice;
/* S5b 恢复总线用：枚举时的 HOST_CONTROL 与时钟，供 WifiFw.c 里的消息通道复用 */
UINT8 gWifiHostControl;
UINT32 gWifiRestoreHertz;

/* 每条失败路径统一走这里：打一行原因 + 置失败态（避免"静默返回"查不到信息） */
int SdioFail(const char *Where) {
    SdhciMark("WiFi: FAIL at ");
    SdhciMark(Where);
    SdhciMark("\n");
    gWifiSdioState = -1;
    return -1;
}

/*
 * 供 shell 的 wifiprobe 调：Force=1 清掉"只试一次"的状态再跑。
 * 不清 SD0 的 Ready（启动盘不受影响）。
 */
int SdhciWifiProbe(UINT32 Flags) {
    SDHCI_HOST *Host = SdhciHostAt(1);

    if ((Flags & SDHCI_WIFI_PROBE_WAKE) != 0) {
        DuoWifiWakeDrive(1);
    }
    if ((Flags & SDHCI_WIFI_PROBE_FORCE) != 0) {
        gWifiSdioState = 0;
        if (Host != 0) {
            Host->Ready = 0;
            Host->CardType = SDHCI_CARD_NONE;
            Host->BusWidth = 1;
            Host->HighCapacity = 0;
            Host->RelativeCardAddress = 0;
        }
        SdhciMark("WiFi: state cleared for retry\n");
    }
    return SdhciWifiInitialize();
}

/*
 * 显式 FIFO 自检入口（shell: wififw fifotest）。只做人工诊断用：
 * 无论成败都不改 Host->Ready / gWifiSdioState —— 枚举成功与否只由
 * CMD5→3→7 + CCCR + CIS 决定。
 */
int SdhciWifiFifoTest(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT8 Caps = 0;

    if (Host == 0 || gWifiSdioState != 1) {
        SdhciMark("WiFi: fifotest needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CAPS, &Caps) != 0) {
        Caps = 0;
    }
    /* 走字节模式的路径必须先确认 fn1 0x07 的允许位（厂商 V3 写 0x01） */
    {
        UINT8 Bm = 0;
        if (SdhciIoRead8(Host, 1, AIC_BYTEMODE_ENABLE_REG_V3, &Bm) == 0 && (Bm & 0x01u) == 0) {
            (void)SdhciIoWrite8(Host, 1, AIC_BYTEMODE_ENABLE_REG_V3, 0x01u);
            (void)SdhciIoRead8(Host, 1, AIC_BYTEMODE_ENABLE_REG_V3, &Bm);
            SdhciMarkHex("WiFi: fifotest set fn1 0x07 -> ", Bm, 2);
            SdhciMark("\n");
        }
    }
    SdhciMark("WiFi: fifotest begin (explicit, not part of enum)\n");
    {
        int Rc = SdioFifoTest(Host, (Caps & SDIO_CAP_SMB) ? 1 : 0);
        SdhciMark(Rc == 0 ? "WiFi: fifotest end ok (state untouched)\n"
                          : "WiFi: fifotest end FAILED (state untouched)\n");
        return Rc;
    }
}

/* S5b 用：枚举是否成功 / 总线卡住时自救 / 身份是否 D80 */
int SdhciWifiReady(void) {
    return (gWifiSdioState == 1) ? 1 : 0;
}


int SdhciWifiChipIsD80(void) {
    return (gWifiChipVendor == 0xC8A1u && gWifiChipDevice == 0x0082u) ? 1 : 0;
}


int SdhciWifiBusRecover(SDHCI_HOST *Host) {
    if (Host == 0) {
        return -1;
    }
    return SdioRecoverBus(Host, gWifiHostControl, gWifiRestoreHertz);
}


/*
 * Sdio.c 的对外主入口：顺序表。每一步自己完成日志/寄存器/失败处理，
 * 这里只负责顺序与短路；顺序与原 494 行函数逐条一致。
 */
int SdhciWifiInitialize(void) {
    WIFI_PROBE Context;
    int Rc = ProbePrepare(&Context);

    if (Rc != 0) {
        return (Rc > 0) ? 0 : -1;
    }
    if (ControllerBringUp(&Context) != 0) {
        return -1;
    }
    if (ModulePowerOn(&Context) != 0) {
        return -1;
    }
    if (CardEnumerate(&Context) != 0) {
        return -1;
    }
    if (CccrConfigure(&Context) != 0) {
        return -1;
    }
    if (SpeedSwitch(&Context) != 0) {
        return -1;
    }
    if (BusWidthSwitch(&Context) != 0) {
        return -1;
    }
    if (FunctionEnable(&Context) != 0) {
        return -1;
    }
    if (CisVerify(&Context) != 0) {
        return -1;
    }
    if (CisReport(&Context) != 0) {
        return -1;
    }
    return ProbeFinish(&Context);
}
