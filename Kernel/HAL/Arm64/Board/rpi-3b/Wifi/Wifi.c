/*
 * Wifi.c — Pi3B WiFi 门面：探针 + 灌片 + HalWifi ops（pi-wifi-6）
 * 0x141：Claim 前就写 WIFI-SCAN.LOG（status=）；灌片卡住也有落盘面包屑。
 * 勿改 Shell/Commands/Wifi.c（Duo weak）。
 */
#include "Wifi/Private.h"
#include "HalDevices.h"
#include "FileSystem.h"
#include "Fat.h"

static int WifiOpsScan(void);

static void WifiAppendText(char *Buffer, UINTN Capacity, UINTN *Length, const char *Text) {
    if (Buffer == 0 || Length == 0 || Text == 0) {
        return;
    }
    while (*Text != 0 && *Length + 1u < Capacity) {
        Buffer[*Length] = *Text;
        (*Length)++;
        Text++;
    }
}

static void WifiAppendDecimal(char *Buffer, UINTN Capacity, UINTN *Length, UINT32 Value) {
    UINT32 Digits = 0;
    char Reverse[12];

    if (Value == 0u) {
        Reverse[Digits++] = '0';
    }
    while (Value > 0u && Digits < sizeof(Reverse)) {
        Reverse[Digits++] = (char)('0' + (Value % 10u));
        Value /= 10u;
    }
    while (Digits > 0u) {
        Digits--;
        if (*Length + 1u < Capacity) {
            Buffer[(*Length)++] = Reverse[Digits];
        }
    }
}

static void WifiBootMarkScanResults(void) {
    UINT32 Index;

    WifiBootMarkHexadecimal32("Boot: Pi WIFI scan count=", gWifiScanEntryCount);
    HalSerialBootMarkForce("\n");
    for (Index = 0; Index < gWifiScanEntryCount; Index++) {
        HalSerialBootMarkForce("Boot: Pi WIFI ssid=");
        HalSerialBootMarkForce(gWifiScanEntries[Index].Ssid);
        WifiBootMarkHexadecimal32(" rssi=", (UINT32)(INT32)gWifiScanEntries[Index].SignalRssi);
        WifiBootMarkHexadecimal32(" ch=", gWifiScanEntries[Index].Channel);
        HalSerialBootMarkForce("\n");
    }
}

/*
 * CH 专读 WIFI-SCAN.LOG。灌片前先写 status=claim-begin，避免卡在 Load 时「完全无文件」。
 */
static void WifiPersistStatusLog(const char *Status) {
    char Buffer[2048];
    UINTN Length = 0;
    UINT32 Index;
    static const char HexDigits[] = "0123456789abcdef";
    UINT32 Tag = WIFI_BUILD_TAG;
    INT32 Nibble;

    WifiAppendText(Buffer, sizeof(Buffer), &Length, "tag=");
    for (Nibble = 7; Nibble >= 0; Nibble--) {
        if (Length + 1u >= sizeof(Buffer)) {
            break;
        }
        Buffer[Length++] = HexDigits[(Tag >> (UINT32)(Nibble * 4)) & 0xFu];
    }
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nstatus=");
    WifiAppendText(Buffer, sizeof(Buffer), &Length, Status ? Status : "?");
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\npresent=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, gWifiFirmwarePresent ? 1u : 0u);
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nloaded=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, gWifiFirmwareLoaded ? 1u : 0u);
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\ncount=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, gWifiScanEntryCount);
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\npoll_empty=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanPollEmptyCount());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\npoll_error=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanPollErrorCount());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nevent_pkts=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanEventPacketCount());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nescan_partial=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanEscanPartialCount());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nioctl_status=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, gWifiIoctlLastStatus);
    /* 0x14e：首包眼睛（判定 type 偏移 / ethertype 是否进 Handle） */
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nethertype_ok=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanEtherTypeOkCount());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nbdc_off=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanFirstBdcDataOffset());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\ntype_at2=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanFirstEventTypeAt2());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\ntype_at4=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanFirstEventTypeAt4());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nstatus_at6=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanFirstEventStatusAt6());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nstatus_at8=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanFirstEventStatusAt8());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\nelen=");
    WifiAppendDecimal(Buffer, sizeof(Buffer), &Length, WifiScanFirstEventLength());
    WifiAppendText(Buffer, sizeof(Buffer), &Length, "\n");
    for (Index = 0; Index < gWifiScanEntryCount; Index++) {
        WifiAppendText(Buffer, sizeof(Buffer), &Length, "ssid=");
        WifiAppendText(Buffer, sizeof(Buffer), &Length, gWifiScanEntries[Index].Ssid);
        WifiAppendText(Buffer, sizeof(Buffer), &Length, "\n");
    }
    if (Length >= sizeof(Buffer)) {
        Length = sizeof(Buffer) - 1u;
    }
    Buffer[Length] = 0;
    if (FileSystemWriteFile("WIFI-SCAN.LOG", Buffer, Length) != FAT_OK) {
        HalSerialBootMarkForce("Boot: Pi WIFI scan log write fail\n");
        return;
    }
    (void)FileSystemFileSync("WIFI-SCAN.LOG");
    HalSerialBootMarkForce("Boot: Pi WIFI scan log written status=");
    HalSerialBootMarkForce(Status ? Status : "?");
    HalSerialBootMarkForce("\n");
}

static int WifiOpsClaim(void) {
    if (!gWifiChipProbed) {
        WifiPersistStatusLog("no-chip");
        return 0;
    }
    if (gWifiFirmwareLoaded) {
        WifiPersistStatusLog("already-loaded");
        return 1;
    }
    WifiFirmwarePathProbe();
    if (!gWifiFirmwarePresent) {
        WifiPersistStatusLog("fw-missing");
        return 1;
    }
    /* 灌片前落盘：若此后卡死，CH 仍见 claim-begin */
    WifiPersistStatusLog("claim-begin");
    if (WifiFirmwareLoad() != 0) {
        const char *Stage = WifiFirmwareLoadFailStage();

        HalSerialBootMarkForce("Boot: Pi WIFI claim load fail\n");
        /* 0x148：status=load-* 阶段字，勿笼统 claim-load-fail */
        WifiPersistStatusLog(Stage && Stage[0] ? Stage : "claim-load-fail");
        return 1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI claim load ok\n");
    WifiPersistStatusLog("claim-load-ok");
    /*
     * 0x143：0x142 CH 仍停在 claim-load-ok、无 scan-begin → Worker 里
     * HalWifiReady/Scan 分支未执行到。灌片成功后在 Claim 内直接扫。
     */
    (void)WifiOpsScan();
    return 1;
}

static int WifiOpsReady(void) {
    return (gWifiChipProbed && gWifiFirmwareLoaded) ? 1 : 0;
}

int BoardWifiClaim(void) {
    return WifiOpsClaim();
}

int BoardWifiReady(void) {
    return WifiOpsReady();
}

int BoardWifiScan(void) {
    return WifiScanAccessPoints();
}

int BoardWifiAssociateOpen(const char *Ssid) {
    return WifiAssociateOpen(Ssid);
}

UINT32 BoardWifiScanEntryCount(void) {
    return gWifiScanEntryCount;
}

const BOARD_WIFI_SCAN_ENTRY *BoardWifiScanEntryAt(UINT32 Index) {
    if (Index >= gWifiScanEntryCount) {
        return 0;
    }
    return &gWifiScanEntries[Index];
}

static int WifiOpsScan(void) {
    /* 0x142：先落 scan-begin；radio 失败也要写 status（0x141 停在 claim-load-ok） */
    WifiPersistStatusLog("scan-begin");
    if (WifiRadioBringUp() != 0) {
        const char *Detail = WifiRadioFailStatus();

        HalSerialBootMarkForce("Boot: Pi WIFI radio bring-up fail\n");
        /* 0x147：country-fail / up-fail / getup-fail，勿笼统 radio-fail */
        WifiPersistStatusLog(Detail && Detail[0] ? Detail : "radio-fail");
        return -1;
    }
    WifiPersistStatusLog("radio-up");
    if (WifiScanAccessPoints() != 0) {
        const char *Detail = WifiScanFailStatus();

        HalSerialBootMarkForce("Boot: Pi WIFI scan fail\n");
        WifiBootMarkScanResults();
        /* 0x144：勿再笼统 scan-fail；CH 看 escan-fail vs scan-empty + poll_* */
        WifiPersistStatusLog(Detail && Detail[0] ? Detail : "scan-fail");
        return -1;
    }
    WifiBootMarkScanResults();
    WifiPersistStatusLog("scan-ok");
    HalSerialBootMarkForce("Boot: Pi WIFI wifi-6 scan ok\n");
    return 0;
}

static UINT32 WifiOpsScanCount(void) {
    return gWifiScanEntryCount;
}

static int WifiOpsScanEntryAt(UINT32 Index, char *SsidOut, UINT32 SsidCapacity, INT32 *RssiOut,
                              UINT16 *ChannelOut) {
    const BOARD_WIFI_SCAN_ENTRY *Entry;
    UINT32 Copy;
    UINT32 ByteIndex;

    Entry = BoardWifiScanEntryAt(Index);
    if (Entry == 0 || SsidOut == 0 || SsidCapacity == 0) {
        return -1;
    }
    Copy = Entry->SsidLength;
    if (Copy >= SsidCapacity) {
        Copy = SsidCapacity - 1u;
    }
    for (ByteIndex = 0; ByteIndex < Copy; ByteIndex++) {
        SsidOut[ByteIndex] = Entry->Ssid[ByteIndex];
    }
    SsidOut[Copy] = 0;
    if (RssiOut != 0) {
        *RssiOut = Entry->SignalRssi;
    }
    if (ChannelOut != 0) {
        *ChannelOut = Entry->Channel;
    }
    return 0;
}

static int WifiOpsAssociateOpen(const char *Ssid) {
    return WifiAssociateOpen(Ssid);
}

static int WifiProbeOnce(void) {
    WifiPinConfigure();
    if (WifiWirelessPowerOn() != 0) {
        return -1;
    }
    if (WifiSdioHostBringUp() != 0) {
        return -1;
    }
    if (WifiSdioEnumerate() != 0) {
        return -1;
    }
    if (WifiSdioEnableFunction1() != 0) {
        return -1;
    }
    if (WifiChipIdentityProbe() != 0) {
        return -1;
    }
    WifiBringUpDiagnosticsPrint();
    {
        const char *Path = WifiFirmwarePathForChip();
        HalSerialBootMark("Boot: Pi WIFI fw path=");
        HalSerialBootMark(Path ? Path : "(none)");
        HalSerialBootMark(" (load after FS)\n");
    }
    return 0;
}

void BoardWifiRegister(void) {
    static const HAL_WIFI_OPS Ops = {
        .Claim = WifiOpsClaim,
        .Ready = WifiOpsReady,
        .Scan = WifiOpsScan,
        .ScanCount = WifiOpsScanCount,
        .ScanEntryAt = WifiOpsScanEntryAt,
        .AssociateOpen = WifiOpsAssociateOpen,
    };

    HalWifiSetOps(&Ops);
    if (WifiProbeOnce() != 0) {
        HalSerialBootMark("Boot: Pi WIFI probe fail (Ready=0)\n");
        gWifiChipProbed = 0;
        return;
    }
    HalSerialBootMark("Boot: Pi WIFI ok\n");
}
