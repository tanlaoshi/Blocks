/*
 * Diagnostics.c — pwifi-2 解封第 1 步：只读诊断（KSO / GPCLK2 / SDIO core rev）
 *
 * 不改任何寄存器。对表 Linux brcmfmac：sdio.c:3498 kso_init、sdio.h:109 SLEEPCSR、
 * chip.c:905 DMP EROM 扫描（core rev 来源）。串口一行：
 *   Boot: Pi WIFI kso=0x?? gp2=0x???????? gp2div=0x???????? sdioRev=0x??
 */
#include "Wifi/Private.h"

#define WIFI_SDIO_SLEEP_STATUS 0x1001Fu
#define WIFI_CHIPCOMMON_EROM_POINTER 0xFCu
#define WIFI_CORE_ID_SDIO_DEVICE 0x829u

/* DMP 描述符（对表 brcmfmac chip.c） */
#define WIFI_EROM_DESCRIPTOR_TYPE_MASK 0x0000000Fu
#define WIFI_EROM_DESCRIPTOR_VALID 0x00000001u
#define WIFI_EROM_DESCRIPTOR_COMPONENT 0x00000001u
#define WIFI_EROM_DESCRIPTOR_MASTER_PORT 0x00000003u
#define WIFI_EROM_DESCRIPTOR_ADDRESS 0x00000005u
#define WIFI_EROM_DESCRIPTOR_ADDRESS_SIZE_ABOVE_32 0x00000008u
#define WIFI_EROM_DESCRIPTOR_END_OF_TABLE 0x0000000Fu

#define WIFI_EROM_COMPONENT_PART_NUMBER_MASK 0x000FFF00u
#define WIFI_EROM_COMPONENT_PART_NUMBER_SHIFT 8u
#define WIFI_EROM_COMPONENT_REVISION_MASK 0xFF000000u
#define WIFI_EROM_COMPONENT_REVISION_SHIFT 24u
#define WIFI_EROM_COMPONENT_MASTER_WRAPPER_MASK 0x0007C000u
#define WIFI_EROM_COMPONENT_MASTER_WRAPPER_SHIFT 14u
#define WIFI_EROM_COMPONENT_SLAVE_WRAPPER_MASK 0x00F80000u
#define WIFI_EROM_COMPONENT_SLAVE_WRAPPER_SHIFT 19u

#define WIFI_EROM_SLAVE_TYPE_MASK 0x000000C0u
#define WIFI_EROM_SLAVE_TYPE_SHIFT 6u
#define WIFI_EROM_SLAVE_TYPE_SLAVE 0u
#define WIFI_EROM_SLAVE_TYPE_SLAVE_WRAPPER 2u
#define WIFI_EROM_SLAVE_TYPE_MEMORY_WRAPPER 3u
#define WIFI_EROM_SLAVE_SIZE_TYPE_MASK 0x00000030u
#define WIFI_EROM_SLAVE_SIZE_TYPE_SHIFT 4u
#define WIFI_EROM_SLAVE_SIZE_4K 0u
#define WIFI_EROM_SLAVE_SIZE_8K 1u
#define WIFI_EROM_SLAVE_SIZE_DESCRIPTOR 3u
#define WIFI_EROM_SLAVE_ADDRESS_BASE_MASK 0xFFFFF000u

#define WIFI_EROM_DESCRIPTOR_MAXIMUM 512u
#define WIFI_SDIO_CORE_REVISION_WITH_KSO 12u

static void WifiDiagnosticsPrintHexadecimal(const char *Name, UINT32 Value, int Digits) {
    char Buffer[17];

    HalSerialBootMarkForce(Name);
    HalSerialFormatHex(Buffer, Value, Digits);
    HalSerialBootMarkForce(Buffer);
}

static int WifiEromDescriptorRead(UINT32 *Address, UINT32 *OutValue, UINT8 *OutType) {
    UINT32 Value;
    UINT8 Type;

    if (Address == 0 || OutValue == 0) {
        return -1;
    }
    if (WifiBackplaneRead32(*Address, &Value) != 0) {
        return -1;
    }
    *Address += 4u;
    *OutValue = Value;
    if (OutType != 0) {
        Type = (UINT8)(Value & WIFI_EROM_DESCRIPTOR_TYPE_MASK);
        if (((UINT32)Type & ~WIFI_EROM_DESCRIPTOR_ADDRESS_SIZE_ABOVE_32) ==
            WIFI_EROM_DESCRIPTOR_ADDRESS) {
            Type = (UINT8)WIFI_EROM_DESCRIPTOR_ADDRESS;
        }
        *OutType = Type;
    }
    return 0;
}

/* 跳过本 core 的 master-port / address / size 描述符，停在下一个 component 前 */
static int WifiEromAddressRegionSkip(UINT32 *Address) {
    UINT32 Value = 0;
    UINT32 UpperValue = 0;
    UINT32 SizeDescriptor = 0;
    UINT8 DescriptorType = 0;
    UINT8 WrapperType;
    UINT8 SlaveType;
    UINT8 SizeType;
    UINT32 RegisterBase = 0;
    UINT32 WrapperBase = 0;

    if (WifiEromDescriptorRead(Address, &Value, &DescriptorType) != 0) {
        return -1;
    }
    if (DescriptorType == WIFI_EROM_DESCRIPTOR_MASTER_PORT) {
        WrapperType = (UINT8)WIFI_EROM_SLAVE_TYPE_MEMORY_WRAPPER;
    } else if (DescriptorType == WIFI_EROM_DESCRIPTOR_ADDRESS) {
        *Address -= 4u;
        WrapperType = (UINT8)WIFI_EROM_SLAVE_TYPE_SLAVE_WRAPPER;
    } else {
        *Address -= 4u;
        return -1;
    }

    do {
        do {
            if (WifiEromDescriptorRead(Address, &Value, &DescriptorType) != 0) {
                return -1;
            }
            if (DescriptorType == WIFI_EROM_DESCRIPTOR_END_OF_TABLE) {
                *Address -= 4u;
                return -1;
            }
        } while (DescriptorType != WIFI_EROM_DESCRIPTOR_ADDRESS &&
                 DescriptorType != WIFI_EROM_DESCRIPTOR_COMPONENT);

        if (DescriptorType == WIFI_EROM_DESCRIPTOR_COMPONENT) {
            *Address -= 4u;
            return 0;
        }
        if ((Value & WIFI_EROM_DESCRIPTOR_ADDRESS_SIZE_ABOVE_32) != 0) {
            if (WifiEromDescriptorRead(Address, &UpperValue, 0) != 0) {
                return -1;
            }
        }
        SizeType = (UINT8)((Value & WIFI_EROM_SLAVE_SIZE_TYPE_MASK) >>
                           WIFI_EROM_SLAVE_SIZE_TYPE_SHIFT);
        if (SizeType == WIFI_EROM_SLAVE_SIZE_DESCRIPTOR) {
            if (WifiEromDescriptorRead(Address, &SizeDescriptor, 0) != 0) {
                return -1;
            }
            if ((SizeDescriptor & WIFI_EROM_DESCRIPTOR_ADDRESS_SIZE_ABOVE_32) != 0) {
                if (WifiEromDescriptorRead(Address, &SizeDescriptor, 0) != 0) {
                    return -1;
                }
            }
        }
        if (SizeType != WIFI_EROM_SLAVE_SIZE_4K && SizeType != WIFI_EROM_SLAVE_SIZE_8K) {
            continue;
        }
        SlaveType =
            (UINT8)((Value & WIFI_EROM_SLAVE_TYPE_MASK) >> WIFI_EROM_SLAVE_TYPE_SHIFT);
        if (RegisterBase == 0 && SlaveType == WIFI_EROM_SLAVE_TYPE_SLAVE) {
            RegisterBase = Value & WIFI_EROM_SLAVE_ADDRESS_BASE_MASK;
        }
        if (WrapperBase == 0 && SlaveType == WrapperType) {
            WrapperBase = Value & WIFI_EROM_SLAVE_ADDRESS_BASE_MASK;
        }
    } while (RegisterBase == 0 || WrapperBase == 0);
    return 0;
}

/* 对表 brcmf_chip_dmp_erom_scan：只找 SDIO core（0x829）的 CompIdentB rev */
static int WifiSdioCoreRevisionRead(UINT32 *OutRevision) {
    UINT32 EromPointer = 0;
    UINT32 EromAddress;
    UINT32 Value = 0;
    UINT8 DescriptorType = 0;
    UINT32 Walk;
    UINT16 PartNumber;
    UINT8 Revision;
    UINT32 WrapperCount;

    if (OutRevision == 0) {
        return -1;
    }
    if (WifiBackplaneRead32(WIFI_CHIPCOMMON_BASE + WIFI_CHIPCOMMON_EROM_POINTER,
                            &EromPointer) != 0) {
        return -1;
    }
    if (EromPointer == 0 || EromPointer == 0xFFFFFFFFu) {
        return -1;
    }
    EromAddress = EromPointer;
    for (Walk = 0; Walk < WIFI_EROM_DESCRIPTOR_MAXIMUM; Walk++) {
        if (WifiEromDescriptorRead(&EromAddress, &Value, &DescriptorType) != 0) {
            return -1;
        }
        if (DescriptorType == WIFI_EROM_DESCRIPTOR_END_OF_TABLE) {
            break;
        }
        if ((Value & WIFI_EROM_DESCRIPTOR_VALID) == 0) {
            continue;
        }
        if (DescriptorType != WIFI_EROM_DESCRIPTOR_COMPONENT) {
            continue;
        }
        PartNumber = (UINT16)((Value & WIFI_EROM_COMPONENT_PART_NUMBER_MASK) >>
                              WIFI_EROM_COMPONENT_PART_NUMBER_SHIFT);
        if (WifiEromDescriptorRead(&EromAddress, &Value, &DescriptorType) != 0) {
            return -1;
        }
        if ((Value & WIFI_EROM_DESCRIPTOR_TYPE_MASK) != WIFI_EROM_DESCRIPTOR_COMPONENT) {
            return -1;
        }
        Revision = (UINT8)((Value & WIFI_EROM_COMPONENT_REVISION_MASK) >>
                           WIFI_EROM_COMPONENT_REVISION_SHIFT);
        if (PartNumber == WIFI_CORE_ID_SDIO_DEVICE) {
            *OutRevision = Revision;
            return 0;
        }
        /* 无端口 core 直接跳过（对表 get_regaddr 回退后 continue） */
        WrapperCount =
            ((Value & WIFI_EROM_COMPONENT_MASTER_WRAPPER_MASK) >>
             WIFI_EROM_COMPONENT_MASTER_WRAPPER_SHIFT) +
            ((Value & WIFI_EROM_COMPONENT_SLAVE_WRAPPER_MASK) >>
             WIFI_EROM_COMPONENT_SLAVE_WRAPPER_SHIFT);
        if (WrapperCount == 0) {
            continue;
        }
        if (WifiEromAddressRegionSkip(&EromAddress) != 0) {
            return -1;
        }
    }
    return -1;
}

void WifiBringUpDiagnosticsPrint(void) {
    UINT8 SleepStatus = 0;
    UINT32 Gp2Control;
    UINT32 Gp2Divider;
    UINT32 SdioRevision = 0;
    int SleepReadOk;
    int RevisionReadOk;

    SleepReadOk =
        (WifiSdioCommand52Read(SDIO_FUNCTION_1, WIFI_SDIO_SLEEP_STATUS, &SleepStatus) == 0);
    Gp2Control = *(volatile UINT32 *)(UINTN)(WIFI_CLOCK_MANAGER_BASE +
                                             WIFI_CLOCK_MANAGER_GP2_CONTROL_OFFSET);
    Gp2Divider = *(volatile UINT32 *)(UINTN)(WIFI_CLOCK_MANAGER_BASE +
                                             WIFI_CLOCK_MANAGER_GP2_DIVIDER_OFFSET);
    RevisionReadOk = (WifiSdioCoreRevisionRead(&SdioRevision) == 0);

    HalSerialBootMarkForce("Boot: Pi WIFI kso=");
    if (SleepReadOk) {
        WifiDiagnosticsPrintHexadecimal("0x", SleepStatus, 2);
    } else {
        HalSerialBootMarkForce("read-fail");
    }
    HalSerialBootMarkForce(" gp2=");
    WifiDiagnosticsPrintHexadecimal("0x", Gp2Control, 8);
    HalSerialBootMarkForce(" gp2div=");
    WifiDiagnosticsPrintHexadecimal("0x", Gp2Divider, 8);
    HalSerialBootMarkForce(" sdioRev=");
    if (RevisionReadOk) {
        WifiDiagnosticsPrintHexadecimal("0x", SdioRevision, 2);
        if (SdioRevision < WIFI_SDIO_CORE_REVISION_WITH_KSO) {
            HalSerialBootMarkForce(" (<12 no KSO)");
        } else {
            HalSerialBootMarkForce(" (>=12 needs KSO)");
        }
    } else {
        HalSerialBootMarkForce("read-fail");
    }
    HalSerialBootMarkForce("\n");
}

/* 存储阵列探针：先读、再写读回，把「读不通」与「只有写不通」分开 */
void WifiDiagnosticsSramAccessProbe(void) {
    UINT32 ProbeValue = 0;
    UINT32 ProbeAddress = WIFI_RAM_DOWNLOAD_BASE + 0x100u;

    if (WifiBackplaneWindowSet(WIFI_RAM_DOWNLOAD_BASE) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI window set fail\n");
        return;
    }
    if (WifiBackplaneRead32(ProbeAddress, &ProbeValue) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram read32 fail\n");
        return;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI sram read first=", ProbeValue);
    HalSerialBootMarkForce("\n");
    if (WifiBackplaneWrite32(ProbeAddress, 0xBEEF1234u) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram write32 fail\n");
        return;
    }
    if (WifiBackplaneRead32(ProbeAddress, &ProbeValue) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram read back fail\n");
        return;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI sram probe=", ProbeValue);
    HalSerialBootMarkForce("\n");
}
