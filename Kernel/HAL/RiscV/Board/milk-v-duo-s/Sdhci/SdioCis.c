/*
 * SdioCis.c — SDIO 身份识别：CIS 指针/解析/扫描/打印、芯片名与身份判定、fn1 寄存器现场
 *
 * 职责边界：只回答"这张卡是谁"——读 CIS、认 vendor/device、打 fn1 现场；
 *           不做数据搬运（SdioTransfer.c）、不做整机枚举（Sdio.c）。
 * 从 Sdio.c 原样搬出（纯搬家，函数体逐字不动），行为零变化。
 */
#include "Sdhci/Private.h"



int SdioReadCisPointer(SDHCI_HOST *Host, UINT32 Function, UINT32 *Pointer) {
    UINT32 Ptr = 0;
    UINT32 i;
    UINT8 Byte;

    for (i = 0; i < 3u; i++) {
        if (SdhciIoRead8(Host, 0, Function * 0x100u + SDIO_FBR_CIS + i, &Byte) != 0) {
            return -1;
        }
        Ptr |= (UINT32)Byte << (i * 8u);
    }
    *Pointer = Ptr & 0x1FFFFu;
    return 0;
}


int SdioParseCis(SDHCI_HOST *Host, UINT32 Pointer, SDIO_CIS_INFO *Info) {
    UINT32 Ptr = Pointer;
    UINT32 Guard;

    Info->Vendor = 0;
    Info->Device = 0;
    Info->FunctionClass = 0xFFu;
    Info->MaxBlockSize = 0;
    Info->SawManfid = 0;
    if (Pointer == 0 || Pointer == 0x1FFFFu) {
        return -1;
    }
    for (Guard = 0; Guard < 64u; Guard++) {
        UINT8 Code;
        UINT8 Length;
        UINT8 Data[4];
        UINT32 i;

        if (Ptr > 0x1FFF0u) {
            return -1;
        }
        if (SdhciIoRead8(Host, 0, Ptr, &Code) != 0) {
            return -1;
        }
        Ptr++;
        if (Code == 0xFFu) {
            return 0;
        }
        if (Code == 0x00u) {
            continue; /* 空条目：只有 code，没有 link/data */
        }
        if (SdhciIoRead8(Host, 0, Ptr, &Length) != 0) {
            return -1;
        }
        Ptr++;
        if (Length == 0xFFu) {
            return 0;
        }
        if (Code == CISTPL_MANFID && Length >= 4u) {
            for (i = 0; i < 4u; i++) {
                if (SdhciIoRead8(Host, 0, Ptr + i, &Data[i]) != 0) {
                    return -1;
                }
            }
            Info->Vendor = (UINT16)((UINT16)Data[0] | ((UINT16)Data[1] << 8));
            Info->Device = (UINT16)((UINT16)Data[2] | ((UINT16)Data[3] << 8));
            Info->SawManfid = 1;
        } else if (Code == CISTPL_FUNCID && Length >= 1u) {
            if (SdhciIoRead8(Host, 0, Ptr, &Data[0]) != 0) {
                return -1;
            }
            Info->FunctionClass = Data[0];
        } else if (Code == CISTPL_FUNCE && Length >= 3u) {
            for (i = 0; i < 3u; i++) {
                if (SdhciIoRead8(Host, 0, Ptr + i, &Data[i]) != 0) {
                    return -1;
                }
            }
            Info->MaxBlockSize = (UINT16)((UINT16)Data[1] | ((UINT16)Data[2] << 8));
        }
        Ptr += Length;
    }
    return -1; /* 元组过多：当失败，不无限扫 */
}

/* 把一段 CIS 的原始字节打出来；读失败就标出偏移，一眼能看出是"读到 00"还是"读不动" */
void SdioDumpCis(SDHCI_HOST *Host, const char *Tag, UINT32 Address, UINT32 Count) {
    UINT32 i;
    UINT8 Byte;
    int Rc;

    SdhciMark(Tag);
    for (i = 0; i < Count; i++) {
        Rc = SdhciIoRead8(Host, 0, Address + i, &Byte);
        if (Rc != 0) {
            SdhciMarkHex(" <read fail +", i, 2);
            SdhciMark(">");
            break;
        }
        SdhciMarkHex(" ", Byte, 2);
    }
    SdhciMark("\n");
}

/*
 * 在 [From,To) 里找 MANFID 元组头 0x20 0x04（MANFID 的 link 固定是 4，
 * 见 sdio_cis.c 的 CIS_TUPLE(MANFID) 表）。返回元组起始地址，找不到返回 0。
 * 只在"指针解析不出 MANFID"时当兜底用，顺序读、每字节一次 CMD52。
 */
UINT32 SdioScanCis(SDHCI_HOST *Host, UINT32 From, UINT32 To) {
    UINT32 Addr;
    UINT8 Prev = 0;
    UINT8 Byte;

    if (SdhciIoRead8(Host, 0, From, &Prev) != 0) {
        return 0;
    }
    for (Addr = From + 1u; Addr < To; Addr++) {
        if (SdhciIoRead8(Host, 0, Addr, &Byte) != 0) {
            return 0;
        }
        if (Prev == CISTPL_MANFID && Byte == 4u) {
            return Addr - 1u;
        }
        Prev = Byte;
    }
    return 0;
}


void SdioMarkCis(UINT32 Function, UINT32 Pointer, const SDIO_CIS_INFO *Info) {
    SdhciMarkHex("WiFi: CIS fn", Function, 1);
    SdhciMarkHex(" ptr=", Pointer, 6);
    SdhciMarkHex(" manfid=", Info->Vendor, 4);
    SdhciMarkHex(" device=", Info->Device, 4);
    SdhciMarkHex(" class=", Info->FunctionClass, 2);
    SdhciMarkHex(" blk=", Info->MaxBlockSize, 4);
    SdhciMark("\n");
}

/*
 * 厂商芯片表（aicwf_sdio.h:75-80，判定逻辑 aicsdio.c:239-259 / aicwf_sdio.c:528-552）：
 *   AIC8801    : vendor 0x5449 / device 0x0145
 *   AIC8800DC  : vendor 0xc8a1 / device 0xc08d
 *   AIC8800D80 : vendor 0xc8a1 / device 0x0082   ← Duo S 板载这颗
 * 注意：厂商"认芯片"用的是 **CIS 里的 vendor/device**，不是读某个寄存器。
 */
static const char *SdioChipName(UINT16 Vendor, UINT16 Device) {
    if (Vendor == 0xC8A1u && Device == 0x0082u) {
        return "aic8800d80";
    }
    if (Vendor == 0xC8A1u && Device == 0xC08Du) {
        return "aic8800dc";
    }
    if (Vendor == 0x5449u && Device == 0x0145u) {
        return "aic8801";
    }
    return 0;
}


int SdioChipIdentify(void) {
    const char *Name = SdioChipName(gWifiChipVendor, gWifiChipDevice);

    if (Name == 0) {
        SdhciMarkHex("WiFi: chip id=unknown manfid=", gWifiChipVendor, 4);
        SdhciMarkHex(" pid=", gWifiChipDevice, 4);
        SdhciMark("\n");
        SdhciMark("WiFi:   vendor table: 8801=5449/0145 8800dc=c8a1/c08d 8800d80=c8a1/0082\n");
        return -1;
    }
    SdhciMark("WiFi: chip id=");
    SdhciMark(Name);
    SdhciMarkHex(" (manfid=", gWifiChipVendor, 4);
    SdhciMarkHex(" pid=", gWifiChipDevice, 4);
    SdhciMark(")\n");
    /*
     * chip_rev（0x40500000）在 AIC 里**不是寄存器读**，要发 dbg-mem-read 消息走 msg 通道
     * （aic_bsp_driver.c:1876-1880、1924-1929），而它只用来在 fw_8800d80_u01/u02 之间选；
     * SDK 里 D80 只发了 u02 一套（fmacfw_8800d80_u02.bin 等），所以 S5b 直接锁 u02。
     */
    SdhciMark("WiFi: fw profile=8800d80_u02 (only u02 ships; chip_rev read deferred)\n");
    return 0;
}

/*
 * fn1 的 V3 寄存器快照（aicsdio.h:35-48 的地址表）：只读，不写一个字节。
 * 00 INTR_ENABLE / 01 INTR_PENDING / 02 INTR_TO_DEVICE / 03 FLOW_CTRL_Q1 /
 * 04 MISC_INT_STATUS / 05 BYTEMODE_LEN / 06 BYTEMODE_LEN_MSB / 07 BYTEMODE_ENABLE /
 * 08 MISC_CTRL / 09 FLOW_CTRL_Q2 / 0A CLK_TEST_RESULT
 */
void SdioMarkFn1Regs(SDHCI_HOST *Host) {
    static const UINT8 Off[11] = {0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u,
                                  0x06u, 0x07u, 0x08u, 0x09u, 0x0Au};
    static const char *const Name[11] = {"ien", "ipd", "itd", "fc1", "misc", "len",
                                         "lenh", "bme", "mctl", "fc2", "clk"};
    UINT32 i;
    UINT8 Byte;

    SdhciMark("WiFi: fn1 regs");
    for (i = 0; i < 11u; i++) {
        Byte = 0;
        SdhciMark(" ");
        SdhciMark(Name[i]);
        SdhciMark("=");
        if (SdhciIoRead8(Host, 1, Off[i], &Byte) != 0) {
            SdhciMark("xx");
        } else {
            SdhciMarkHex("", Byte, 2);
        }
    }
    SdhciMark("\n");
}

/* CMD5 现场：PRESENT_STATE / 原始 INT_STATUS / 高 16 位错误位 / INT_ENABLE / 时钟 */
void SdioMarkCmd5State(SDHCI_HOST *Host, const char *Tag) {
    UINT32 St = SdhciRead32(Host, SDHCI_INT_STATUS);

    SdhciMark(Tag);
    SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
    SdhciMarkHex(" int=", St, 8);
    SdhciMarkHex(" err=", (St >> 16) & 0xFFFFu, 4);
    SdhciMarkHex(" ien=", SdhciRead32(Host, SDHCI_INT_ENABLE), 8);
    SdhciMarkHex(" clk=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
    SdhciMark("\n");
}

/*
 * CMD5 探活窗口（厂商给芯片 2000 ms，见 aicsdio.c:534）。
 * 两道门：
 *   1) 进循环前先看 CMD_INHIBIT：卡住就先 RESET_ALL + 400k 把命令 FSM 拽回来，
 *      还卡就直接判死，不白跑 12 次（08:24 那次就是这个形态）。
 *   2) 每次失败后再看一次，一旦 inhibit 卡住立刻跳出去做同一套恢复。
 * 返回 0 = 通了。
 */
int SdioCmd5Window(SDHCI_HOST *Host) {
    UINT16 Flags = (UINT16)SDHCI_CMD_RESP_SHORT; /* R4 无 CRC / 无 index */
    UINT32 Pst;
    UINT32 Round;
    UINT32 i;
    UINT32 Tries;

    for (Round = 0; Round < 2u; Round++) {
        Tries = (Round == 0) ? DUO_WIFI_CMD5_TRIES : 4u;
        Pst = SdhciRead32(Host, SDHCI_PRESENT_STATE);
        if ((Pst & SDHCI_CMD_INHIBIT) != 0) {
            SdhciMark("WiFi: CMD5 blocked by CMD_INHIBIT (cmd line busy)\n");
            SdioMarkCmd5State(Host, "WiFi:  ");
            if (Round != 0) {
                return -1;
            }
            SdhciMark("WiFi: recover: RESET_ALL + 400kHz\n");
            if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0 ||
                SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
                return -1;
            }
            continue;
        }
        for (i = 0; i < Tries; i++) {
            if (SdhciCommandSend(Host, 5, 0, Flags, 0) == 0) {
                SdhciMarkDecimal("WiFi: CMD5 ok after #", i + 1u);
                SdhciMark("\n");
                return 0;
            }
            SdhciMarkDecimal("WiFi: CMD5 #", i + 1u);
            SdhciMarkHex(" st=", Host->LastStatus, 8);
            SdhciMarkHex(" pst=", Host->LastPresentState, 8);
            SdhciMark("\n");
            if ((Host->LastPresentState & SDHCI_CMD_INHIBIT) != 0) {
                SdhciMark("WiFi: CMD_INHIBIT stuck after this try\n");
                break;
            }
            SdhciDelayMilliseconds(DUO_WIFI_CMD5_GAP_MS);
        }
        if (Round != 0) {
            break;
        }
        SdhciMark("WiFi: recover: RESET_ALL + 400kHz\n");
        if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0 ||
            SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
            return -1;
        }
    }
    return -1;
}
