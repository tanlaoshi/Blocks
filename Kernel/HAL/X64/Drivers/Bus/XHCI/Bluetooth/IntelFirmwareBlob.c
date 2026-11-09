/*
 * IntelFirmwareBlob.c — ibt-*.sfi 路径拼装 + HalDriverFileRead
 */
#include "XHCI/Bluetooth/Private.h"
#include "HalDriverFile.h"
#include "PhysicalMemory.h"

#define INTEL_FIRMWARE_MAX_BYTES (1024u * 1024u)
#define INTEL_RSA_HEADER_LENGTH 644u

static int BluetoothIntelAppendDecimal(char *Out, UINTN Cap, UINTN *Pos,
                                       UINT32 Value) {
    char Digits[10];
    UINT32 N = 0u;

    if (Out == 0 || Pos == 0 || Cap < 1u) {
        return -1;
    }
    do {
        Digits[N++] = (char)('0' + (Value % 10u));
        Value /= 10u;
    } while (Value != 0u && N < sizeof(Digits));
    while (N > 0u) {
        N--;
        if (*Pos + 1u >= Cap) {
            return -1;
        }
        Out[(*Pos)++] = Digits[N];
    }
    return 0;
}

int BluetoothIntelBuildFirmwarePath(char *Out, UINTN Cap, UINT8 HardwareVariant,
                                    UINT16 DeviceRevisionId) {
    /* 对表 IWL：NUC 系统盘已有 FW/；Assets/Firmware/intel 需另同步目录 */
    const char *Prefix = "FW/ibt-";
    UINTN Pos = 0u;
    UINTN I;

    if (Out == 0 || Cap < 40u) {
        return -1;
    }
    for (I = 0; Prefix[I]; I++) {
        Out[Pos++] = Prefix[I];
    }
    if (BluetoothIntelAppendDecimal(Out, Cap, &Pos, HardwareVariant) < 0) {
        return -1;
    }
    if (Pos + 1u >= Cap) {
        return -1;
    }
    Out[Pos++] = '-';
    if (BluetoothIntelAppendDecimal(Out, Cap, &Pos, DeviceRevisionId) < 0) {
        return -1;
    }
    if (Pos + 5u >= Cap) {
        return -1;
    }
    Out[Pos++] = '.';
    Out[Pos++] = 's';
    Out[Pos++] = 'f';
    Out[Pos++] = 'i';
    Out[Pos] = 0;
    return 0;
}

int BluetoothIntelReadFirmwareBlob(const char *Path, UINT8 **OutBlob,
                                   UINTN *OutBytes, UINT32 *OutPages) {
    UINT8 Probe[16];
    UINTN Size = 0;
    UINTN Got = 0;
    UINT32 Pages;
    void *Memory;
    int Error;

    *OutBlob = 0;
    *OutBytes = 0;
    *OutPages = 0;
    Error = HalDriverFileRead(Path, Probe, sizeof(Probe), &Size);
    if (Error != 0 || Size < 4u) {
        BootLog("Boot: Bluetooth Intel .sfi miss\n");
        return -1;
    }
    Pages = (INTEL_FIRMWARE_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
    Memory = PhysicalMemoryAllocatePages(Pages);
    if (Memory == 0) {
        BootLog("Boot: Bluetooth Intel .sfi oom\n");
        return -1;
    }
    Error = HalDriverFileRead(Path, Memory, Pages * PAGE_SIZE, &Got);
    if (Error != 0 || Got < INTEL_RSA_HEADER_LENGTH ||
        Got > INTEL_FIRMWARE_MAX_BYTES) {
        BootLog("Boot: Bluetooth Intel .sfi read fail\n");
        PhysicalMemoryFreePages(Memory, Pages);
        return -1;
    }
    *OutBlob = (UINT8 *)Memory;
    *OutBytes = Got;
    *OutPages = Pages;
    BootLogHex("Boot: Bluetooth Intel .sfi bytes=", (UINT64)Got, 8);
    return 0;
}
