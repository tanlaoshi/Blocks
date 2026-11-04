/*
 * WifiFirmwareCheck.c — wififw 纯读检查 / 文件 stat / 芯片版本
 *
 * 自 WifiFw.c 拆出（行数债）；行为不变。
 */
#include "WifiPrivate.h"

/*
 * 检查模式（评审 P0-3）：**纯读**。
 * 不写任何寄存器（不跑 vendor init）、不发任何消息（不读 chip rev），
 * 只 stat 文件 + 读缓存里的身份结论，把"将要写什么"打全。
 * 通道是否可用由 `wififw id` 单独证明，本命令不再替它"顺手探一下"。
 */
int WifiFirmwareCheck(SDHCI_HOST *Host) {
    UINT32 Index;
    UINT32 Size = 0;
    int Result;
    UINT32 TableLength = 0;

    (void)Host;
    SdhciMarkDecimal("WiFi: fw check: buf=", AIC_FW_CHUNK);
    SdhciMark(" blk=1024 dir=" AIC_FW_DIR "\n");
    SdhciMark("WiFi: fw check: read-only (files only, no register write, no msg)\n");
    if (AicFwLoadPatchTable(Host, gAicFwBuffer, AIC_FW_CHUNK, &TableLength) != 0) {
        SdhciMark("WiFi: fw check FAILED: patch table unusable (refuse to guess addr)\n");
        return -1;
    }
    SdhciMark("WiFi: fw check: id=");
    SdhciMark(SdhciWifiChipIsD80() != 0 ? "aic8800d80 (c8a1:0082)\n"
                                        : "unknown (wififw write will refuse)\n");
    for (Index = 0; Index < AIC_FW_STAGE_MAX; Index++) {
        AIC_FW_STAGE *Stage = &gAicStages[Index];
        SdhciMarkDecimal("WiFi:  stage ", Index + 1u);
        SdhciMark("/");
        SdhciMarkDecimal("", AIC_FW_STAGE_MAX);
        SdhciMark(" ");
        SdhciMark(Stage->Name);
        if (Stage->Kind == AIC_STAGE_PATCHTBL) {
            SdhciMark(" table=" AIC_FW_PATCH_TABLE " (100 writes + cfg 14 ops)\n");
            continue;
        }
        if (Stage->Kind == AIC_STAGE_START) {
            SdhciMarkHex(" bootaddr=", Stage->Address, 8);
            SdhciMark(" (msg DBG_START_APP_REQ)\n");
            continue;
        }
        SdhciMark(" file=");
        SdhciMark(Stage->Path);
        Result = AicFwCheckStage(Stage, &Size);
        if (Result == 1) {
            SdhciMark(" (optional, missing -> skip)\n");
            continue;
        }
        if (Result != 0) {
            SdhciMark(" (WOULD FAIL)\n");
            return -1;
        }
        SdhciMarkDecimal(" size=", Size);
        SdhciMarkHex(" -> ", Stage->Address, 8);
        SdhciMark(Stage->Required != 0 ? " (required)\n" : " (optional)\n");
    }
    SdhciMark("WiFi: fw check done, NOTHING WRITTEN (no reg write, no msg)\n");
    SdhciMark("WiFi: fw check: channel not probed here, run 'wififw id' for that\n");
    return 0;
}

/*
 * 评审 W10：这个命令以前叫 `read` 却只做 stat，名不副实。
 * 现在叫 `stat`，并且输出里明说"仅 stat、不读内容"。
 */
int WifiFirmwareStatCheck(void) {
    FAT_FILE_STAT Stat;
    int Missing = 0;

    SdhciMark("WiFi: fw stat only (no content read)\n");
    if (FileSystemFileStat(AIC_FW_ADID, &Stat) == FAT_OK) {
        SdhciMarkDecimal("WiFi:  fw_adid.bin ", Stat.Size);
        SdhciMark("B ok\n");
    } else {
        SdhciMark("WiFi:  fw_adid.bin MISSING\n");
        Missing = 1;
    }
    if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) == FAT_OK) {
        SdhciMarkDecimal("WiFi:  fw_patch_table.bin ", Stat.Size);
        SdhciMark("B ok\n");
    } else {
        SdhciMark("WiFi:  fw_patch_table.bin MISSING\n");
        Missing = 1;
    }
    if (FileSystemFileStat(AIC_FW_FMAC, &Stat) == FAT_OK) {
        SdhciMarkDecimal("WiFi:  fmacfw.bin ", Stat.Size);
        SdhciMark("B ok\n");
    } else {
        SdhciMark("WiFi:  fmacfw.bin MISSING (write confirm needs it)\n");
        Missing = 1;
    }
    return (Missing != 0) ? -1 : 0;
}

/* 只读：读芯片版本（0x40500000），同时证明消息通道可用 */
int WifiFirmwareChipRevision(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 Value = 0;

    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: fw needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: vendor init failed\n");
        return -1;
    }
    if (AicMemRead(Host, AIC_D80_CHIP_REV_ADDR, &Value) != 0) {
        SdhciMark("WiFi: chip rev read failed (msg channel not up)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: mem rd addr=", AIC_D80_CHIP_REV_ADDR, 8);
    SdhciMarkHex(" data=", Value, 8);
    SdhciMark("\n");
    SdhciMarkHex("WiFi: chip rev data=", Value, 8);
    SdhciMarkHex(" rev=", (Value >> 16) & 0xFFu, 2);
    SdhciMark("\n");
    return 0;
}
