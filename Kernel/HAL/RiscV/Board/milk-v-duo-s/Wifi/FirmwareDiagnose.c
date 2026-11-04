/*
 * WifiFirmwareDiagnose.c — wififw rd / wr 诊断（mem 读回对账）
 *
 * 自 WifiFw.c 拆出（行数债）；行为不变。
 */
#include "Wifi/Private.h"

/*
 * 只读诊断：`wififw rd <hexaddr>`。真写之后要能读回内存对账，这是唯一手段。
 * 只做准备动作 + 一次 mem read，不改 patch 状态、不发任何写消息。
 */
int SdhciWifiFwRead(UINT32 Address) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 Value = 0;

    if ((Address & 3u) != 0u) {
        SdhciMarkHex("WiFi: mem rd refused: not 4-aligned addr=", Address, 8);
        SdhciMark("\n");
        return -1;
    }
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: mem rd needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: mem rd: vendor prepare failed\n");
        return -1;
    }
    if (AicMemRead(Host, Address, &Value) != 0) {
        SdhciMarkHex("WiFi: mem rd FAIL addr=", Address, 8);
        SdhciMarkHex(" r5=", Host->LastR5, 8);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkHex("WiFi: mem rd addr=", Address, 8);
    SdhciMarkHex(" data=", Value, 8);
    SdhciMark("\n");
    return 0;
}

/*
 * 单字写诊断：`wififw wr <addr> <val>`。
 * 走 DBG_MEM_WRITE_REQ，写完立刻读回。
 * 护栏：只允许
 *   [0x00100000, 0x00210000)  RAM
 *   [0x40500000, 0x40510000)  外设寄存器
 */
int SdhciWifiFwWriteDiag(UINT32 Address, UINT32 Value) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 ReadBack = 0;

    if ((Address & 3u) != 0u) {
        SdhciMarkHex("WiFi: mem wr refused: not 4-aligned addr=", Address, 8);
        SdhciMark("\n");
        return -1;
    }
    if (!((Address >= 0x00100000u && Address < 0x00210000u) ||
          (Address >= 0x40500000u && Address < 0x40510000u))) {
        SdhciMarkHex("WiFi: mem wr refused: addr outside allowed windows ", Address, 8);
        SdhciMark("\n");
        SdhciMark("WiFi: allowed: [00100000,0020ffff] ram, [40500000,4050ffff] regs\n");
        return -1;
    }
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: mem wr needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: mem wr: vendor prepare failed\n");
        return -1;
    }
    SdhciMarkHex("WiFi: mem wr addr=", Address, 8);
    SdhciMarkHex(" val=", Value, 8);
    SdhciMark("\n");
    if (AicMemWrite32(Host, Address, Value) != 0) {
        SdhciMarkHex("WiFi: mem wr FAIL addr=", Address, 8);
        SdhciMarkHex(" r5=", Host->LastR5, 8);
        SdhciMark("\n");
        return -1;
    }
    if (AicMemRead(Host, Address, &ReadBack) != 0) {
        SdhciMarkHex("WiFi: mem wr readback FAIL addr=", Address, 8);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkHex("WiFi: mem wr readback=", ReadBack, 8);
    if (ReadBack == Value) {
        SdhciMark(" (match)\n");
        return 0;
    }
    SdhciMarkHex(" (MISMATCH, wrote ", Value, 8);
    SdhciMark(")\n");
    return -1;
}
