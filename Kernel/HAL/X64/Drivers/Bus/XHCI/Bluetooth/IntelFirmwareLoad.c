/*
 * IntelFirmwareLoad.c — Intel USB HCI bootloader → operational（对表 btintel）
 *
 * 序：先读盘 .sfi（免 HCI 中段长空窗）→ 0xFC05/0xFC0D → RSA+载荷 0xFC09
 *     → vendor 0xFF/0x06 → 0xFC01 → vendor 0xFF/0x02 → 标准 HCI_Reset。
 */
#include "XHCI/Bluetooth/Private.h"
#include "Hal.h"
#include "PhysicalMemory.h"

#define INTEL_FW_VARIANT_BOOTLOADER 0x06u
#define INTEL_FW_VARIANT_OPERATIONAL 0x23u
#define INTEL_HARDWARE_VARIANT_WSP 0x0Cu
#define INTEL_DOWNLOAD_WAIT_MS 5000u
#define INTEL_BOOT_WAIT_MS 5000u
#define INTEL_VENDOR_EVENT 0xFFu
#define INTEL_VENDOR_DOWNLOAD_COMPLETE 0x06u
#define INTEL_VENDOR_BOOT_COMPLETE 0x02u
#define INTEL_PRODUCT_WSP 0x0A2Bu
#define INTEL_DEFAULT_WSP_PATH "FW/ibt-12-16.sfi"

static int BluetoothIntelWaitVendorSubtype(UINT8 Subtype, UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;

    for (;;) {
        /* 已有事件（如 FC01 后 boot-complete 先到）勿再 Arm 清掉 */
        if (!gBluetoothEventDone) {
            BluetoothHciArmEvent();
            if (BluetoothHciWaitArmed(TimeoutMs) < 0) {
                if (ReadTsc() - Start >= Need) {
                    return -1;
                }
                continue;
            }
        } else {
            FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
        }
        if (gBluetoothEventBuffer[0] == INTEL_VENDOR_EVENT &&
            gBluetoothEventBuffer[1] >= 1u &&
            gBluetoothEventBuffer[2] == Subtype) {
            return 0;
        }
        gBluetoothEventDone = 0;
        if (ReadTsc() - Start >= Need) {
            BootLogHex("Boot: Bluetooth Intel vendor wait got=",
                       gBluetoothEventBuffer[0], 2);
            return -1;
        }
    }
}

static int BluetoothIntelReadVersion(UINT8 *FirmwareVariantOut,
                                     UINT8 *HardwareVariantOut) {
    UINT8 Status = 0xFFu;
    UINT8 Version[12];
    UINT8 ReturnLength = 0;

    ZeroMemory(Version, sizeof(Version));
    if (BluetoothHciCommand(HCI_OPCODE_INTEL_VERSION, 0, 0, &Status, Version,
                            sizeof(Version), &ReturnLength) < 0 ||
        Status != 0u || ReturnLength < 4u) {
        return -1;
    }
    if (HardwareVariantOut) {
        *HardwareVariantOut = Version[1];
    }
    if (FirmwareVariantOut) {
        *FirmwareVariantOut = Version[3];
    }
    BootLogHex("Boot: Bluetooth Intel hw_variant=", Version[1], 2);
    BootLogHex(" fw_variant=", Version[3], 2);
    return 0;
}

static int BluetoothIntelReadBootParameters(UINT16 *DeviceRevisionIdOut,
                                            UINT8 *LimitedCommandCompleteOut) {
    UINT8 Status = 0xFFu;
    UINT8 Params[32];
    UINT8 ReturnLength = 0;
    UINT16 DeviceRevisionId;

    ZeroMemory(Params, sizeof(Params));
    if (BluetoothHciCommand(HCI_OPCODE_INTEL_BOOT_PARAMETERS, 0, 0, &Status,
                            Params, sizeof(Params), &ReturnLength) < 0) {
        return -1;
    }
    if (Status != 0u || ReturnLength < 21u) {
        BootLogHex("Boot: Bluetooth Intel boot-params status=", Status, 2);
        BootLogHex(" retlen=", ReturnLength, 2);
        return -1;
    }
    DeviceRevisionId = (UINT16)(Params[3] | ((UINT16)Params[4] << 8));
    if (DeviceRevisionIdOut) {
        *DeviceRevisionIdOut = DeviceRevisionId;
    }
    if (LimitedCommandCompleteOut) {
        *LimitedCommandCompleteOut = Params[20];
    }
    BootLogHex("Boot: Bluetooth Intel dev_revid=", DeviceRevisionId, 4);
    BootLogHex(" limited_cce=", Params[20], 2);
    return 0;
}

static int BluetoothIntelBootOperational(void) {
    UINT8 ResetParams[8];

    /* intel_reset；bootloader 不回 CC（对表 btusb inject_cmd_complete） */
    ResetParams[0] = 0x00u;
    ResetParams[1] = 0x01u;
    ResetParams[2] = 0x00u;
    ResetParams[3] = 0x01u;
    ResetParams[4] = 0x00u;
    ResetParams[5] = 0x00u;
    ResetParams[6] = 0x00u;
    ResetParams[7] = 0x00u;
    BluetoothHciArmEvent();
    if (BluetoothHciSendRaw(HCI_OPCODE_INTEL_RESET, ResetParams, 8) < 0) {
        BootLog("Boot: Bluetooth Intel Reset send fail\n");
        return -1;
    }
    if (BluetoothIntelWaitVendorSubtype(INTEL_VENDOR_BOOT_COMPLETE,
                                        INTEL_BOOT_WAIT_MS) < 0) {
        BootLog("Boot: Bluetooth Intel boot-complete timeout\n");
        return -1;
    }
    HalDelayMilliseconds(100u);
    BootLog("Boot: Bluetooth Intel boot-complete\n");
    return 0;
}

int BluetoothIntelFirmwareLoad(void) {
    UINT8 FirmwareVariant = 0;
    UINT8 HardwareVariant = 0;
    UINT16 DeviceRevisionId = 0;
    UINT8 LimitedCommandComplete = 0xFFu;
    char Path[64];
    UINT8 *Blob = 0;
    UINTN Bytes = 0;
    UINT32 Pages = 0;
    UINT8 OperationalVariant = 0;
    int Preloaded = 0;

    if (gBluetoothVendorId != 0x8087u) {
        return 0;
    }
    /*
     * 8087:0A2B：先读盘再打 HCI。U 盘 MSC 读 584KB 若夹在 FC0D 与 FC09
     * 之间，真机首片 secure-send 无事件（xfer-cc=0）。
     */
    if (gBluetoothProductId == INTEL_PRODUCT_WSP) {
        if (BluetoothIntelReadFirmwareBlob(INTEL_DEFAULT_WSP_PATH, &Blob, &Bytes,
                                           &Pages) == 0) {
            Preloaded = 1;
        }
    }
    if (BluetoothIntelReadVersion(&FirmwareVariant, &HardwareVariant) < 0) {
        BootLog("Boot: Bluetooth Intel ver for fw fail\n");
        if (Blob) {
            PhysicalMemoryFreePages(Blob, Pages);
        }
        return 0;
    }
    if (FirmwareVariant == INTEL_FW_VARIANT_OPERATIONAL) {
        BootLog("Boot: Bluetooth Intel already operational\n");
        if (Blob) {
            PhysicalMemoryFreePages(Blob, Pages);
        }
        return 1;
    }
    if (FirmwareVariant != INTEL_FW_VARIANT_BOOTLOADER) {
        BootLog("Boot: Bluetooth Intel mode=other (no fw load)\n");
        if (Blob) {
            PhysicalMemoryFreePages(Blob, Pages);
        }
        return 0;
    }
    if (HardwareVariant != INTEL_HARDWARE_VARIANT_WSP &&
        HardwareVariant != 0x0Bu) {
        BootLogHex("Boot: Bluetooth Intel hw unsupported=", HardwareVariant, 2);
        if (Blob) {
            PhysicalMemoryFreePages(Blob, Pages);
        }
        return 0;
    }
    if (BluetoothIntelReadBootParameters(&DeviceRevisionId,
                                         &LimitedCommandComplete) < 0) {
        if (Blob) {
            PhysicalMemoryFreePages(Blob, Pages);
        }
        return 0;
    }
    if (LimitedCommandComplete != 0u) {
        BootLog("Boot: Bluetooth Intel limited_cce unsupported\n");
        if (Blob) {
            PhysicalMemoryFreePages(Blob, Pages);
        }
        return 0;
    }
    if (BluetoothIntelBuildFirmwarePath(Path, sizeof(Path), HardwareVariant,
                                        DeviceRevisionId) < 0) {
        if (Blob) {
            PhysicalMemoryFreePages(Blob, Pages);
        }
        return 0;
    }
    BootLog("Boot: Bluetooth Intel fw path=");
    BootLog(Path);
    BootLog("\n");
    if (!Preloaded) {
        if (BluetoothIntelReadFirmwareBlob(Path, &Blob, &Bytes, &Pages) < 0) {
            return 0;
        }
    }
    BootLog("Boot: Bluetooth Intel fw download begin\n");
    if (BluetoothIntelSecureSendRsaHeader(Blob, Bytes) < 0 ||
        BluetoothIntelSecureSendPayload(Blob, Bytes) < 0) {
        PhysicalMemoryFreePages(Blob, Pages);
        BootLog("Boot: Bluetooth Intel fw download fail\n");
        return 0;
    }
    PhysicalMemoryFreePages(Blob, Pages);
    if (BluetoothIntelWaitVendorSubtype(INTEL_VENDOR_DOWNLOAD_COMPLETE,
                                        INTEL_DOWNLOAD_WAIT_MS) < 0) {
        BootLog("Boot: Bluetooth Intel download-complete timeout\n");
        return 0;
    }
    if (gBluetoothEventBuffer[1] >= 2u && gBluetoothEventBuffer[3] != 0u) {
        BootLogHex("Boot: Bluetooth Intel download result=",
                   gBluetoothEventBuffer[3], 2);
        return 0;
    }
    BootLog("Boot: Bluetooth Intel download-complete\n");
    if (BluetoothIntelBootOperational() < 0) {
        return 0;
    }
    if (BluetoothIntelReadVersion(&OperationalVariant, 0) < 0 ||
        OperationalVariant != INTEL_FW_VARIANT_OPERATIONAL) {
        BootLog("Boot: Bluetooth Intel still not operational\n");
        return 0;
    }
    BootLog("Boot: Bluetooth Intel firmware load ok\n");
    return 1;
}
