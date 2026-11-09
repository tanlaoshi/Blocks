/*
 * SdioCisVerify.c — SdhciWifiInitialize 第 8-9 步：CIS 验收与身份报告
 *
 * 职责边界：Common/fn1 CIS 读、MANFID 兜底、芯片识别入口、fn1/fn2 现场；
 *           解析原语在 SdioCis.c；不做枚举/CMD53。
 * 从 SdioCis.c 原样搬出（纯搬家，函数体逐字不动），行为零变化。
 */
#include "Sdhci/Private.h"

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
/*
 * SdhciWifiInitialize 第 8 步：读 CIS（Common + fn1），失败时两级兜底。
 * 原 Sdio.c 451-526；指针/扫描/降速重读的顺序与日志逐字保留。
 */
int CisVerify(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;
    UINT32 CommonPointer = 0;
    UINT32 Fn1Pointer = 0;
    UINT32 Hit = 0;
    SDIO_CIS_INFO Fn1;
    UINT8 HostControl = SdhciRead8(Host, SDHCI_HOST_CONTROL);
    UINT32 RestoreHertz = ((P->Speed & SDIO_SPEED_EHS) != 0) ? TRANSFER_HERTZ : SD_TRANSFER_HERTZ;

    P->CisCommonPointer = 0;
    P->CisOk = -1;
    gWifiHostControl = HostControl;
    gWifiRestoreHertz = RestoreHertz;
    if (SdioReadCisPointer(Host, 0, &CommonPointer) != 0) {
        SdhciMark("WiFi: common CIS pointer (0x09-0x0B) read failed\n");
        CommonPointer = 0;
    }
    if (SdioReadCisPointer(Host, 1, &Fn1Pointer) != 0) {
        return SdioFail("CIS pointer fn1 (0x109-0x10B)");
    }
    SdhciMarkHex("WiFi: cis ptr fn0=", CommonPointer, 6);
    SdhciMarkHex(" fn1=", Fn1Pointer, 6);
    SdhciMark("\n");
    /* 一次把现场抓全：fn1 的 FBR 原文 + 两个 CIS 的头 16 B */
    SdioDumpCis(Host, "WiFi: fbr1 ", 0x100u, 16u);
    if (CommonPointer != 0) {
        SdioDumpCis(Host, "WiFi: cis0 ", CommonPointer, 16u);
        P->CisOk = SdioParseCis(Host, CommonPointer, &P->Cis);
    }
    SdioDumpCis(Host, "WiFi: cis1 ", Fn1Pointer, 16u);
    if (SdioParseCis(Host, Fn1Pointer, &Fn1) == 0) {
        SdioMarkCis(1, Fn1Pointer, &Fn1);
    }

    if (P->CisOk != 0 || P->Cis.SawManfid == 0) {
        /* 兜底 1：直接扫 CIS 区里的 0x20 0x04 */
        Hit = SdioScanCis(Host, 0x1000u, 0x1400u);
        if (Hit != 0) {
            SdhciMarkHex("WiFi: MANFID scan hit ", Hit, 6);
            SdhciMark("\n");
            if (SdioParseCis(Host, Hit, &P->Cis) == 0 && P->Cis.SawManfid != 0) {
                P->CisOk = 0;
            }
        }
    }
    if (P->CisOk != 0 || P->Cis.SawManfid == 0) {
        /* 兜底 2：退回 1-bit / 400 kHz 再读一次 Common CIS（对照 50 MHz 4-bit 的结果） */
        SdhciMark("WiFi: no MANFID yet, retry 1-bit 400kHz\n");
        SdhciWrite8(Host, SDHCI_HOST_CONTROL, 0);
        (void)SdhciClockConfigure(Host, IDENTIFY_HERTZ);
        CommonPointer = 0;
        if (SdioReadCisPointer(Host, 0, &CommonPointer) == 0 && CommonPointer != 0) {
            SdhciMarkHex("WiFi:   low cis0 ptr=", CommonPointer, 6);
            SdhciMark("\n");
            SdioDumpCis(Host, "WiFi:   low cis0 ", CommonPointer, 16u);
            if (SdioParseCis(Host, CommonPointer, &P->Cis) == 0 && P->Cis.SawManfid != 0) {
                P->CisOk = 0;
            }
        }
        SdhciWrite8(Host, SDHCI_HOST_CONTROL, HostControl);
        (void)SdhciClockConfigure(Host, RestoreHertz);
        SdhciMarkHex("WiFi: bus restored hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
        SdhciMarkHex(" clk=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMark("\n");
    }
    P->CisCommonPointer = CommonPointer;
    return 0;
}

/*
 * SdhciWifiInitialize 第 9 步：打印 module/身份、芯片识别、fn1 现场、fn2 CIS。
 * 原 Sdio.c 528-563，只读，不写任何寄存器。
 */
int CisReport(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;

    if (P->CisOk == 0 && P->Cis.SawManfid != 0) {
        SdhciMark("WiFi: module=");
        SdhciMarkHex("", P->Cis.Vendor, 4);
        SdhciMarkHex(" device=", P->Cis.Device, 4);
        SdhciMark((P->Cis.Vendor == 0xC8A1u) ? " (aic8800)\n" : " (unexpected, continue)\n");
        gWifiChipVendor = P->Cis.Vendor;
        gWifiChipDevice = P->Cis.Device;
        P->CisSeen = 1;
        SdioMarkCis(0, P->CisCommonPointer, &P->Cis);
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
        UINT32 Pointer2 = 0;
        SDIO_CIS_INFO Cis2;
        if (SdioReadCisPointer(Host, 2, &Pointer2) == 0 &&
            SdioParseCis(Host, Pointer2, &Cis2) == 0 && Cis2.SawManfid != 0) {
            SdioMarkCis(2, Pointer2, &Cis2);
        }
    }
    return 0;
}
