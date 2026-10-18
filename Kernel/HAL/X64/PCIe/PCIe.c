/*
 * PCIe.c — PCI 配置空间与 USB 控制器枚举（PR-S3-pcie-1）
 *
 * MSI/MSI-X / IOAPIC INTx 见 Msi.c。
 */
#include "PCIe.h"
#include "Text.h"
#include "Console.h"
#include "Debug.h"
#include "Hal.h"

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

UINT32 PciReadConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset) {
    UINT32 Address = (1 << 31) | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC);
    HalIoWrite32(PCI_CONFIG_ADDRESS, Address);
    return HalIoRead32(PCI_CONFIG_DATA);
}

/* 写入 PCI 配置空间 32 位寄存器 */
void PciWriteConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset, UINT32 Value) {
    UINT32 Address = (1 << 31) | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC);
    HalIoWrite32(PCI_CONFIG_ADDRESS, Address);
    HalIoWrite32(PCI_CONFIG_DATA, Value);
}

int PciUsbAt(UINT8 Bus, UINT8 Dev, UINT8 Fn, USB_CONTROLLER *Out) {
    UINT32 VendorDevice;
    UINT32 ClassCode;
    UINT32 Command;
    UINT32 RawBar[6];
    UINT8 Class;
    UINT8 Subclass;
    UINT8 ProgIF;
    int B;

    if (!Out) {
        return 0;
    }
    VendorDevice = PciReadConfig(Bus, Dev, Fn, 0x00);
    if ((VendorDevice & 0xFFFFu) == 0xFFFFu) {
        return 0;
    }
    ClassCode = PciReadConfig(Bus, Dev, Fn, 0x08);
    Class = (UINT8)((ClassCode >> 24) & 0xFFu);
    Subclass = (UINT8)((ClassCode >> 16) & 0xFFu);
    ProgIF = (UINT8)((ClassCode >> 8) & 0xFFu);
    if (Class != 0x0Cu || Subclass != 0x03u) {
        return 0;
    }

    Command = PciReadConfig(Bus, Dev, Fn, 0x04);
    PciWriteConfig(Bus, Dev, Fn, 0x04, Command | 0x07u);

    for (B = 0; B < 6; B++) {
        RawBar[B] = PciReadConfig(Bus, Dev, Fn, (UINT8)(0x10 + B * 4));
        Out->Bar[B] = 0;
    }
    Out->Bus = Bus;
    Out->Device = Dev;
    Out->Function = Fn;
    Out->Type = ProgIF;
    Out->BaseAddress = 0;

    if (ProgIF == 0x00u) {
        for (B = 0; B < 6; B++) {
            if (RawBar[B] & 1u) {
                Out->Bar[B] = (UINT64)(RawBar[B] & 0xFFFCu);
                Out->BaseAddress = Out->Bar[B];
                return 1;
            }
        }
        return 0;
    }

    for (B = 0; B < 6; ) {
        if (RawBar[B] & 1u) {
            B++;
            continue;
        }
        if ((RawBar[B] & 6u) == 4u && B + 1 < 6) {
            Out->Bar[B] = ((UINT64)RawBar[B + 1] << 32) | (RawBar[B] & 0xFFFFFFF0u);
            B += 2;
        } else {
            Out->Bar[B] = (UINT64)(RawBar[B] & 0xFFFFFFF0u);
            B++;
        }
    }
    Out->BaseAddress = Out->Bar[0];
    return Out->BaseAddress != 0;
}

/* 扫描 PCI 总线，将 USB 控制器填入 Controllers 数组，返回找到的数量 */
int PciScanUSBControllers(USB_CONTROLLER *Controllers, int MaxControllers) {
    int Count = 0;
    int Found = 0;
    
    DebugWrite("PCI Scanning...\n");
    
    for (int Bus = 0; Bus < 256; Bus++) {
        for (int Device = 0; Device < 32; Device++) {
            for (int Function = 0; Function < 8; Function++) {
                UINT32 VendorDevice = PciReadConfig(Bus, Device, Function, 0x00);
                UINT16 VendorID = VendorDevice & 0xFFFF;
                
                if (VendorID == 0xFFFF) continue;
                
                UINT32 ClassCode = PciReadConfig(Bus, Device, Function, 0x08);
                UINT8 Class = (ClassCode >> 24) & 0xFF;
                UINT8 Subclass = (ClassCode >> 16) & 0xFF;
                UINT8 ProgIF = (ClassCode >> 8) & 0xFF;
                
                if (Class == 0x0C && Subclass == 0x03) {
                    if (Count >= MaxControllers) return Count;

                    UINT32 Command = PciReadConfig((UINT8)Bus, (UINT8)Device, (UINT8)Function, 0x04);
                    Command |= 0x07;
                    PciWriteConfig((UINT8)Bus, (UINT8)Device, (UINT8)Function, 0x04, Command);

                    UINT32 RawBar[6];
                    for (int B = 0; B < 6; B++) {
                        RawBar[B] = PciReadConfig((UINT8)Bus, (UINT8)Device, (UINT8)Function, (UINT8)(0x10 + B * 4));
                    }
                    for (int B = 0; B < 6; B++) {
                        Controllers[Count].Bar[B] = 0;
                    }

                    /*
                     * UHCI（ProgIF 00）：寄存器在 I/O BAR（常 BAR4），非 MMIO。
                     * 旧逻辑跳过 bit0=1 的 BAR → UHCI 永远扫不到（PR-H-uhci-1）。
                     */
                    if (ProgIF == 0x00) {
                        UINT16 IoBase = 0;
                        int Bi;
                        for (Bi = 0; Bi < 6; Bi++) {
                            if (RawBar[Bi] & 1u) {
                                IoBase = (UINT16)(RawBar[Bi] & 0xFFFCu);
                                Controllers[Count].Bar[Bi] = IoBase;
                                break;
                            }
                        }
                        if (IoBase == 0) {
                            continue;
                        }
                        Controllers[Count].Bus = (UINT8)Bus;
                        Controllers[Count].Device = (UINT8)Device;
                        Controllers[Count].Function = (UINT8)Function;
                        Controllers[Count].BaseAddress = IoBase;
                        Controllers[Count].Type = ProgIF;
#if TOY_KERNEL_DEBUG
                        DebugWrite("USB: UHCI io=");
                        DebugWrite(Uint64ToHex(IoBase));
                        DebugWrite("\n");
#endif
                        Count++;
                        Found = 1;
                        continue;
                    }

                    for (int B = 0; B < 6; ) {
                        if (RawBar[B] & 1) {
                            B++;
                            continue;
                        }
                        if ((RawBar[B] & 6) == 4 && B + 1 < 6) {
                            Controllers[Count].Bar[B] =
                                ((UINT64)RawBar[B + 1] << 32) | (RawBar[B] & 0xFFFFFFF0);
                            B += 2;
                        } else {
                            Controllers[Count].Bar[B] = RawBar[B] & 0xFFFFFFF0;
                            B++;
                        }
                    }

                    UINT64 BaseAddress = Controllers[Count].Bar[0];
                    if (BaseAddress == 0) {
                        continue;
                    }

                    Controllers[Count].Bus = (UINT8)Bus;
                    Controllers[Count].Device = (UINT8)Device;
                    Controllers[Count].Function = (UINT8)Function;
                    Controllers[Count].BaseAddress = BaseAddress;
                    Controllers[Count].Type = ProgIF;

#if TOY_KERNEL_DEBUG
                    {
                        char *TypeStr;
                        if (ProgIF == 0x10) TypeStr = "OHCI";
                        else if (ProgIF == 0x20) TypeStr = "EHCI";
                        else if (ProgIF == 0x30) TypeStr = "XHCI";
                        else TypeStr = "USB";
                        DebugWrite("USB: ");
                        DebugWrite(TypeStr);
                        DebugWrite(" at ");
                        DebugWrite(Uint64ToHex(BaseAddress));
                        DebugWrite("\n");
                    }
#endif
                    
                    Count++;
                    Found = 1;
                }
            }
        }
    }
    
    if (!Found) {
        DebugWrite("No USB Controller found!\n");
    } else {
        DebugWrite("PCI Scan Complete\n");
    }
    
    return Count;
}

/* 在配置空间能力链中查找指定 Cap ID，返回偏移或 0（PR-DEV-irq-mode：导出供枚举记录能力） */
