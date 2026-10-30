/*
 * Sdio.c — Duo S SD1（wifi-sd）SDIO 枚举：对外入口与主流程
 *
 * 本文件只放"从插卡到可用"的主流程与对外入口：
 *   SdhciWifiProbe / SdhciWifiFifoTest / SdhciWifiReady / SdhciWifiChipIsD80 /
 *   SdhciWifiBusRecover / SdhciWifiInitialize
 * 其余按职责拆到同目录：
 *   SdioCis.c      卡是谁（CIS 解析、身份判定、fn1 现场）
 *   SdioTransfer.c 数据怎么走（CMD53 收发、错误位、总线恢复）
 *   SdioSelfTest.c 通道能不能用（自检查询）
 *
 * 时序全部对照 Archive/DUO-WiFi-SDIO时序.md §2 与厂商驱动。
 *
 * 行数说明：本文件当前 ~590 行，超过 .c 的 500 行硬上限，原因是
 * `SdhciWifiInitialize` 一个函数就有 496 行（原 707-1202）。本刀只搬家不改逻辑，
 * 该函数原样保留；按计划下一刀专门拆它，拆完本文件约 100 行。
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
static int SdioFail(const char *Where) {
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


int SdhciWifiInitialize(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 Ocr = 0;
    UINT32 Offer;
    UINT32 i;
    UINT32 NoResponse = 0;
    UINT16 Flags;
    UINT16 Version;
    UINT8 Data;
    UINT8 Speed;
    UINT8 Caps = 0;
    SDIO_CIS_INFO Cis;
    int CisSeen = 0; /* Cis 是否真的被解析成功过（评审 P0-1：先初始化再判） */
    UINT32 Present;

    /* 无条件先清零，避免任何"没解析就用残值"的路径（评审 P0-1） */
    {
        UINT8 *Raw = (UINT8 *)&Cis;
        UINT32 k;
        for (k = 0; k < (UINT32)sizeof(Cis); k++) {
            Raw[k] = 0;
        }
    }

    /* 无条件第一行：证明本函数被调到、以及跑的是哪一次构建 */
    SdhciMark("WiFi: probe start build=" __DATE__ " " __TIME__ "\n");

    if (gWifiSdioState != 0) {
        SdhciMark(gWifiSdioState > 0 ? "WiFi: skip (already ok; use wifiprobe to retry)\n"
                                     : "WiFi: skip (last=fail; use wifiprobe to retry)\n");
        return (gWifiSdioState > 0) ? 0 : -1;
    }
    if (Host == 0) {
        return SdioFail("SdhciHostAt(1)==NULL");
    }

    Version = SdhciRead16(Host, SDHCI_HOST_VERSION);
    SdhciMarkHex("WiFi: SD1 base=", (UINT32)Host->Base, 8);
    SdhciMarkHex(" version=", Version, 4);
    if (Version == 0x0000u || Version == 0xFFFFu) {
        SdhciMark(" (MMIO no response: clock/power/base?)");
    }
    SdhciMark("\n");

    /*
     * ---------- S1：时钟门控（先于控制器复位） ----------
     * Linux 由 CCF 打开 clk_axi4_sd1/clk_sd1/clk_100k_sd1，我们得自己开；
     * 真机上 shift 前的 CLK_EN_0 就是这段要查的东西。
     */
    (void)DuoWifiClockGate();
    DuoWifiPadPeek();

    Host->Specification = (UINT16)(Version & 0xFFu);
    if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0) {
        SdhciMarkHex("WiFi:   SRSI=", SdhciRead8(Host, SDHCI_SOFTWARE_RESET), 2);
        SdhciMarkHex(" clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
        SdhciMark("\n");
        return SdioFail("software reset ALL");
    }
    SdhciMark("Duo SDHCI1 ");
    SdhciMarkHex("ver=", Host->Specification, 8);
    SdhciMarkHex(" cap=", SdhciRead32(Host, SDHCI_CAPABILITIES), 8);
    SdhciMarkHex(" area=", Host->VendorArea, 3);
    SdhciMark("\n");
    SdhciMarkHex("WiFi: MSHC_CTRL=", SdhciRead32(Host, Host->VendorArea + CV_MSHC_CTRL), 8);
    SdhciMarkHex(" phy=", SdhciRead32(Host, Host->VendorArea + CV_PHY_TX_RX), 8);
    SdhciMark("\n");

    /*
     * INT_ENABLE = 0xFFFFFFFF（和 SD0 的 Card.c:52 完全一样）。
     *
     * 这里原来是 0 —— 那是 08:24 那次真机 "CMD5 有去无回、INT_STATUS 恒 0、
     * PRESENT_STATE[0] CMD_INHIBIT 卡死" 的根因：SDHCI 的 Interrupt Status
     * Enable 是状态位的总闸，闸关着 -> 事件不发生 -> 命令完成/超时都看不见 ->
     * 命令 FSM 等软件 W1C 应答等到天荒地老 -> CMD_INHIBIT 恒 1，重发全部
     * 在第一道门就失败。SD0 一直是 0xFFFFFFFF，所以启动盘从没出过这问题。
     */
    if (SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
        SdhciMarkHex("WiFi:   clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
        SdhciMarkHex(" hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
        SdhciMark("\n");
        return SdioFail("clock 400kHz");
    }
    SdhciMarkHex("WiFi: 400k ok clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
    SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
    SdhciMarkHex(" hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
    SdhciMark("\n");

    /*
     * ---------- S1：上电（时钟已经跑起来了，厂商也是先有 400k 再给模块上电） ----------
     * 厂商 aicsdio.c:499-520：gpio 高 → mdelay(50)；这里多一个低 100ms 的断电脉冲，
     * 因为我们是接手 FSBL/U-Boot 留下的现场。
     */
    if (DuoWifiPowerOn() != 0) {
        return SdioFail("GPIOA15 power-on");
    }
    DuoWifiWakePeek();

    /*
     * 卡检测覆盖 + 极性自测（要控制器起来才能读 PRESENT_STATE）。
     * Linux 在 non-removable 上根本不看这两位（sdhci.c:2438-2466 直接 return 1），
     * 所以只打印、不当作继续/放弃的门禁。
     */
    if (DuoWifiCardDetect(Host) != 0) {
        return SdioFail("TOP 0x294 card-detect override");
    }

    Present = SdhciRead32(Host, SDHCI_PRESENT_STATE);
    SdhciMarkHex("SD1: present=", Present, 8);
    SdhciMark(" CARD_INSERTED=");
    SdhciMark((Present & SDHCI_CARD_INSERTED) ? "1" : "0");
    SdhciMark(" CARD_STABLE=");
    SdhciMark((Present & SDHCI_CARD_STABLE) ? "1" : "0");
    SdhciMark("\n");

    /* ---------- S3：CMD0 / CMD5 / CMD3 / CMD7 ---------- */
    (void)SdhciCommandSend(Host, 0, 0, SDHCI_CMD_RESP_NONE, 0);
    SdhciDelayMilliseconds(1);

    /*
     * ---------- S3：CMD5 探活（arg=0，sdio_ops.c mmc_send_io_op_cond 的 "just probing"） ----------
     * 细节在 SdioCmd5Window()：先查 CMD_INHIBIT、失败就 RESET_ALL 重来、共约 2 s 窗口。
     */
    Flags = (UINT16)SDHCI_CMD_RESP_SHORT; /* R4 无 CRC / 无 index */
    SdioMarkCmd5State(Host, "WiFi: pre-CMD5");
    if (SdioCmd5Window(Host) != 0) {
        return SdioFail("CMD5 (no response: module power / SD1 clock or pad)");
    }
    SdhciMarkHex("WiFi: CMD5 r4=", Host->Response[0], 8);
    SdhciMark("\n");

    /*
     * 电压窗口：卡在 R4[23:0] 报它支持的档，主机只报自己能给的（3.3 V 档）。
     * 与 Linux mmc_select_voltage(host, ocr) 同义：取交集，交集为空才用默认 3.2-3.4 V。
     */
    Offer = 0x00300000u & Host->Response[0];
    if (Offer == 0) {
        Offer = 0x00300000u;
    }
    SdhciMarkHex("WiFi: CMD5 offer=", Offer, 8);
    SdhciMark("\n");

    for (i = 0; i < 100u; i++) {
        if (SdhciCommandSend(Host, 5, Offer, Flags, 0) != 0) {
            NoResponse++;
            if (NoResponse >= 3u) {
                /* 连发都不通了：再轮下去只是白等，直接给出寄存器现场 */
                SdhciMarkHex("WiFi: CMD5 send-fail count=0x", NoResponse, 2);
                SdhciMarkHex(" int=", SdhciRead32(Host, SDHCI_INT_STATUS), 8);
                SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
                SdhciMark("\n");
                return SdioFail("CMD5 (keeps failing, cmd line dead)");
            }
            SdhciDelayMilliseconds(10);
            continue;
        }
        NoResponse = 0;
        Ocr = Host->Response[0];
        if ((Ocr & 0x80000000u) != 0) {
            break;
        }
        SdhciDelayMilliseconds(10);
    }
    if ((Ocr & 0x80000000u) == 0) {
        SdhciMarkHex("WiFi: CMD5 responds but not ready ocr=", Ocr, 8);
        SdhciMarkHex(" tries=", i, 2);
        SdhciMark("\n");
        return SdioFail("CMD5 (ready timeout: voltage/power)");
    }
    SdhciMarkHex("WiFi: ocr=", Ocr, 8);
    SdhciMark("\n");

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 3, 0, Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: CMD3 fail");
        return SdioFail("CMD3 (RCA)");
    }
    Host->RelativeCardAddress = Host->Response[0] & 0xFFFF0000u;
    SdhciMarkHex("WiFi: rca=", Host->RelativeCardAddress >> 16, 4);
    SdhciMark("\n");

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 7, Host->RelativeCardAddress, Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: CMD7 fail");
        return SdioFail("CMD7 (select)");
    }

    /* ---------- S3：CCCR（CMD52 读） ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CCCR, &Data) != 0) {
        SdhciMarkHex("WiFi:   CMD52 00 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x00 read (CMD52 path)");
    }
    SdhciMarkHex("WiFi: CCCR 00=", Data, 2);
    SdhciMarkDecimal(" rev=", (UINT32)(Data & 0x0Fu));
    SdhciMarkDecimal(" sdio=", (UINT32)(Data >> 4));
    SdhciMark("\n");

    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CAPS, &Caps) != 0) {
        SdhciMarkHex("WiFi:   CMD52 08 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x08 read");
    }
    SdhciMarkHex("WiFi: CCCR caps=", Caps, 2);
    SdhciMark(" SMB=");
    SdhciMark((Caps & SDIO_CAP_SMB) ? "1" : "0");
    SdhciMark(" SDC=");
    SdhciMark((Caps & SDIO_CAP_SDC) ? "1" : "0");
    SdhciMark(" 4BLS=");
    SdhciMark((Caps & SDIO_CAP_4BLS) ? "1" : "0");
    SdhciMark("\n");

    /* ---------- S3：High-Speed（CCCR 0x13）+ 50 MHz + HISPD ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_SPEED, &Speed) != 0) {
        SdhciMarkHex("WiFi:   CMD52 13 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x13 read (speed)");
    }
    SdhciMarkHex("WiFi: CCCR 13=", Speed, 2);
    SdhciMark(" SHS=");
    SdhciMark((Speed & SDIO_SPEED_SHS) ? "1" : "0");
    SdhciMark("\n");
    if ((Speed & SDIO_SPEED_SHS) != 0) {
        if (SdhciIoWrite8(Host, 0, SDIO_CCCR_SPEED, (UINT8)(Speed | SDIO_SPEED_EHS)) != 0 ||
            SdhciIoRead8(Host, 0, SDIO_CCCR_SPEED, &Speed) != 0) {
            SdhciMarkHex("WiFi:   EHS write r5=", Host->LastStatus, 8);
            SdhciMark("\n");
            return SdioFail("CCCR 0x13 write (HS)");
        }
        SdhciMarkHex("WiFi: CCCR 13->", Speed, 2);
        SdhciMark(" EHS=");
        SdhciMark((Speed & SDIO_SPEED_EHS) ? "1" : "0");
        SdhciMark("\n");
        if ((Speed & SDIO_SPEED_EHS) == 0) {
            return SdioFail("HS switch (card refused EHS)");
        }
    } else {
        SdhciMark("WiFi: card no HS, stay 25MHz\n");
    }
    if (SdhciClockConfigure(Host, (Speed & SDIO_SPEED_EHS) ? TRANSFER_HERTZ : SD_TRANSFER_HERTZ) !=
        0) {
        SdhciMarkHex("WiFi:   clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMark("\n");
        return SdioFail("clock 50MHz");
    }
    /* Linux sdhci.c:2338-2355：MMC_TIMING_SD_HS 时置 HOST_CONTROL[2] */
    if ((Speed & SDIO_SPEED_EHS) != 0) {
        SdhciWrite8(Host, SDHCI_HOST_CONTROL,
                    (UINT8)(SdhciRead8(Host, SDHCI_HOST_CONTROL) | SDHCI_HIGH_SPEED));
    }
    SdhciMark("WiFi: SD1 clk actual=");
    SdhciMarkDecimal("", Host->ActualClockHertz / 1000u);
    SdhciMark("kHz\n");

    /* ---------- S3：4-bit（CCCR 0x07 先卡，HOST_CONTROL 后主机） ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_IF, &Data) != 0) {
        SdhciMarkHex("WiFi:   CMD52 07 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x07 read (bus if)");
    }
    if (SdhciIoWrite8(Host, 0, SDIO_CCCR_IF,
                      (UINT8)((Data & (UINT8)~SDIO_BUS_WIDTH_MASK) | SDIO_BUS_WIDTH_4BIT)) != 0 ||
        SdhciIoRead8(Host, 0, SDIO_CCCR_IF, &Data) != 0 ||
        (Data & SDIO_BUS_WIDTH_MASK) != SDIO_BUS_WIDTH_4BIT) {
        SdhciMarkHex("WiFi: 4bit readback if=", Data, 2);
        SdhciMarkHex(" r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("4-bit switch");
    }
    SdhciWrite8(Host, SDHCI_HOST_CONTROL,
                (UINT8)(SdhciRead8(Host, SDHCI_HOST_CONTROL) | SDHCI_WIDTH_4BIT));
    Host->BusWidth = 4;
    SdhciMarkHex("WiFi: CCCR 07=", Data, 2);
    SdhciMark(" width=4bit\n");

    /* ---------- S3 尾：fn1 块大小（CCCR/FBR 写）+ 使能 fn1 ---------- */
    {
        UINT8 Lo = 0;
        UINT8 Hi = 0;
        if (SdhciIoWrite8(Host, 0, 0x100u + SDIO_FBR_BLKSIZE, 0x00) != 0 ||
            SdhciIoWrite8(Host, 0, 0x101u + SDIO_FBR_BLKSIZE, 0x02) != 0 ||
            SdhciIoRead8(Host, 0, 0x100u + SDIO_FBR_BLKSIZE, &Lo) != 0 ||
            SdhciIoRead8(Host, 0, 0x101u + SDIO_FBR_BLKSIZE, &Hi) != 0) {
            SdhciMarkHex("WiFi:   fn1 blk r5=", Host->LastStatus, 8);
            SdhciMark("\n");
            return SdioFail("fn1 block size");
        }
        SdhciMarkHex("WiFi: fn1 blk=", (UINT32)Lo | ((UINT32)Hi << 8), 4);
        SdhciMark("\n");
    }
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_IOEX, &Data) != 0 ||
        SdhciIoWrite8(Host, 0, SDIO_CCCR_IOEX, (UINT8)(Data | 0x02u)) != 0) {
        SdhciMarkHex("WiFi:   IOEx r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("fn1 enable (IOEx)");
    }
    /*
     * Linux func->enable_timeout 默认 500 ms（sdio_io.c:59-100）。
     * 评审 P0-1：这里以前 `read 失败 → break`，然后拿**上一轮 IOEx 的残值**继续判
     * `bit1 != 0`，于是读失败也会打出 `fn1 enabled` —— 假绿。现在读失败单独判红，
     * 且用独立变量接 IORx，绝不与 IOEx 复用同一个缓冲。
     */
    {
        UINT8 IoRx = 0;
        int ReadErr = 0;
        for (i = 0; i < 500u; i++) {
            if (SdhciIoRead8(Host, 0, SDIO_CCCR_IORX, &IoRx) != 0) {
                ReadErr = 1;
                break;
            }
            if ((IoRx & 0x02u) != 0) {
                break;
            }
            SdhciDelayMilliseconds(1);
        }
        if (ReadErr != 0) {
            SdhciMarkHex("WiFi:   FAIL at IORx read r5=", Host->LastR5, 8);
            SdhciMark("\n");
            return SdioFail("fn1 enable (IORx read)");
        }
        if ((IoRx & 0x02u) == 0) {
            SdhciMarkHex("WiFi:   IORx=", IoRx, 2);
            SdhciMarkHex(" waited=", i, 4);
            SdhciMark(" ms\n");
            return SdioFail("fn1 enable timeout (IORx)");
        }
        SdhciMark("WiFi: fn1 enabled\n");
    }

    /* S3 的验收行：总线（时钟/宽度）+ fn1 都就绪之后才打 */
    SdhciMark((Speed & SDIO_SPEED_EHS) ? "WiFi: SD1 init ok clock=50MHz width=4bit\n"
                                       : "WiFi: SD1 init ok clock=25MHz width=4bit\n");

    /*
     * ---------- S3 验收：读 CIS ----------
     *
     * 上一版在这里判死的根因：**MANFID(0x20) 是 Common CIS 的元组，不在 function CIS 里**
     * （SDIO 规范 §16.4；Linux sdio_cis.c:201 的 sdio_read_common_cis 读 fn0 指针，
     * sdio_read_func_cis 读 fnN 指针）。旧代码拿 fn1 的指针 0x1011 去解析却要求 MANFID，
     * 那个地址是**函数自己的 CIS**（只有 FUNCE 之类的元组），所以 manfid=0000 —— 
     * 指针没错，是找错了地方。
     *
     * 现在：Common CIS（fn0 指针 0x09-0x0B）拿 MANFID/器件号，
     *       fn1 的指针（0x109-0x10B）只拿函数自己的元组（FUNCE 块大小）。
     * 找不到 MANFID 也不再判死：打真实值、扫一遍、必要时降速重读一次，然后继续。
     */
    {
        UINT32 CommonPtr = 0;
        UINT32 Fn1Ptr = 0;
        UINT32 Hit = 0;
        SDIO_CIS_INFO Fn1;
        int CisOk = -1;
        UINT8 HostControl = SdhciRead8(Host, SDHCI_HOST_CONTROL);
        UINT32 RestoreHertz =
            ((Speed & SDIO_SPEED_EHS) != 0) ? TRANSFER_HERTZ : SD_TRANSFER_HERTZ;

        gWifiHostControl = HostControl;
        gWifiRestoreHertz = RestoreHertz;
        if (SdioReadCisPointer(Host, 0, &CommonPtr) != 0) {
            SdhciMark("WiFi: common CIS pointer (0x09-0x0B) read failed\n");
            CommonPtr = 0;
        }
        if (SdioReadCisPointer(Host, 1, &Fn1Ptr) != 0) {
            return SdioFail("CIS pointer fn1 (0x109-0x10B)");
        }
        SdhciMarkHex("WiFi: cis ptr fn0=", CommonPtr, 6);
        SdhciMarkHex(" fn1=", Fn1Ptr, 6);
        SdhciMark("\n");
        /* 一次把现场抓全：fn1 的 FBR 原文 + 两个 CIS 的头 16 B */
        SdioDumpCis(Host, "WiFi: fbr1 ", 0x100u, 16u);
        if (CommonPtr != 0) {
            SdioDumpCis(Host, "WiFi: cis0 ", CommonPtr, 16u);
            CisOk = SdioParseCis(Host, CommonPtr, &Cis);
        }
        SdioDumpCis(Host, "WiFi: cis1 ", Fn1Ptr, 16u);
        if (SdioParseCis(Host, Fn1Ptr, &Fn1) == 0) {
            SdioMarkCis(1, Fn1Ptr, &Fn1);
        }

        if (CisOk != 0 || Cis.SawManfid == 0) {
            /* 兜底 1：直接扫 CIS 区里的 0x20 0x04 */
            Hit = SdioScanCis(Host, 0x1000u, 0x1400u);
            if (Hit != 0) {
                SdhciMarkHex("WiFi: MANFID scan hit ", Hit, 6);
                SdhciMark("\n");
                if (SdioParseCis(Host, Hit, &Cis) == 0 && Cis.SawManfid != 0) {
                    CisOk = 0;
                }
            }
        }
        if (CisOk != 0 || Cis.SawManfid == 0) {
            /* 兜底 2：退回 1-bit / 400 kHz 再读一次 Common CIS（对照 50 MHz 4-bit 的结果） */
            SdhciMark("WiFi: no MANFID yet, retry 1-bit 400kHz\n");
            SdhciWrite8(Host, SDHCI_HOST_CONTROL, 0);
            (void)SdhciClockConfigure(Host, IDENTIFY_HERTZ);
            CommonPtr = 0;
            if (SdioReadCisPointer(Host, 0, &CommonPtr) == 0 && CommonPtr != 0) {
                SdhciMarkHex("WiFi:   low cis0 ptr=", CommonPtr, 6);
                SdhciMark("\n");
                SdioDumpCis(Host, "WiFi:   low cis0 ", CommonPtr, 16u);
                if (SdioParseCis(Host, CommonPtr, &Cis) == 0 && Cis.SawManfid != 0) {
                    CisOk = 0;
                }
            }
            SdhciWrite8(Host, SDHCI_HOST_CONTROL, HostControl);
            (void)SdhciClockConfigure(Host, RestoreHertz);
            SdhciMarkHex("WiFi: bus restored hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
            SdhciMarkHex(" clk=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
            SdhciMark("\n");
        }

        if (CisOk == 0 && Cis.SawManfid != 0) {
            SdhciMark("WiFi: module=");
            SdhciMarkHex("", Cis.Vendor, 4);
            SdhciMarkHex(" device=", Cis.Device, 4);
            SdhciMark((Cis.Vendor == 0xC8A1u) ? " (aic8800)\n" : " (unexpected, continue)\n");
            gWifiChipVendor = Cis.Vendor;
            gWifiChipDevice = Cis.Device;
            CisSeen = 1;
            SdioMarkCis(0, CommonPtr, &Cis);
        } else {
            SdhciMark("WiFi: no MANFID tuple, continue without module id\n");
        }

        /*
         * ---------- S5b-1：芯片识别（只读，不写任何寄存器） ----------
         * 串口判据：`WiFi: chip id=aic8800d80 (manfid=c8a1 pid=0082)`。
         * 判据不到不写固件 —— 这道闸放在下载命令（wififw）里，不拦 S3 枚举，
         * 否则一颗身份异常但枚举正常的卡会把 S3 的验收线也一起弄挂。
         */
        (void)SdioChipIdentify();
        SdioMarkFn1Regs(Host);

        /*
         * fn2 的 CIS 顺手读一下。注意：fn2 **不是**没用的 —— 厂商把消息/确认通道放在
         * fn2（func_msg，aicwf_sdio_recv_pkt 的 msg=1 分支，aicsdio.c:766-772），
         * S5b 的 dbg mem read/write 确认就是从 fn2 0x0F 读回来的。
         */
        {
            UINT32 Ptr2 = 0;
            SDIO_CIS_INFO Cis2;
            if (SdioReadCisPointer(Host, 2, &Ptr2) == 0 &&
                SdioParseCis(Host, Ptr2, &Cis2) == 0 && Cis2.SawManfid != 0) {
                SdioMarkCis(2, Ptr2, &Cis2);
            }
        }
    }

    /*
     * ---------- S3 收尾 ----------
     * **验收线是 CIS，不是 FIFO 回环。** 早期把"写 1536 B 图案再读回来比对"当成功判据，
     * 那是错的：1536 是固件消息补齐后的线上长度（时序表 §2.6），读回来的长度得由
     * fn1 0x04 的待收块数决定，图案回读对不上是正常现象。
     * 更要紧的是：协议还没开始就往发送 FIFO 推 1536 B 垃圾，可能污染 S5 的第一条消息。
     * 所以默认路径**不跑**，只留一句"跳过"，要跑得人工 `wififw fifotest`。
     */
    /* 评审 P0-2：CIS 没过就**不许**打 enum ok；Ready/身份各自独立 */
    SdhciMark(CisSeen != 0 ? "WiFi: sdio enum ok (cis)\n"
                           : "WiFi: sdio enum FAILED (no cis/manfid)\n");
    SdhciMark("WiFi: sdio fifo self-test skipped (run 'wififw fifotest')\n");

    /* fn1 0x04 杂项中断状态：本轮不轮询，只打一次，供 S4 对照 */
    if (SdhciIoRead8(Host, 1, AIC_MISC_INT_STATUS_REG_V3, &Data) == 0) {
        SdhciMarkHex("WiFi: fn1 misc=0x", Data, 2);
        SdhciMark("\n");
    }
    Host->CardType = SDHCI_CARD_SDIO;
    Host->Ready = 1; /* Ready 只表示"总线枚举完了"，与身份无关 */
    gWifiSdioState = 1;
    /*
     * 评审 P1-4：probe 成功说明"芯片重新握过手"，之前那次 patch 的结论不再可信
     * （可能掉过电、可能被 reset）。清掉三态，允许重新走 write confirm。
     */
    {
        extern void SdhciWifiPatchStateReset(void);
        SdhciWifiPatchStateReset();
    }
    /*
     * 评审 P0-2：结论行分三档，身份真的参与验收 ——
     *   cis+id 都有 → probe ok
     *   只有 cis     → probe ok (no identity)（可用，但 wififw 会拒绝写）
     *   cis 都没有   → probe ok (no cis, bus only)
     */
    if (CisSeen != 0 && SdhciWifiChipIsD80() != 0) {
        SdhciMark("WiFi: probe ok (cis+id)\n");
    } else if (CisSeen != 0) {
        SdhciMark("WiFi: probe ok (no identity)\n");
    } else {
        SdhciMark("WiFi: probe ok (no cis, bus only)\n");
    }
    return 0;
}

