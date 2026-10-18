/*
 * TreeDump.c — PR-DEV-tree-lsdev：lsdev -t 树状打印。
 *
 * 根 = Parent==NULL；DeviceGetChildren 扫表取子。缩进 2 空格 × 深度。
 * 深度 ≥16 停止并串口警告（DebugWrite）。有 FriendlyName 用它，
 * 否则 Name + [bus:dev.fn]。-v 时每节点后追加 VID:DID/class/BAR0/IRQ 行。
 */
#include "Device.h"
#include "Debug.h"
#include "Text.h"
#include "HalSerial.h"

static void (*gWrite)(const char *Text);

static void WriteOut(const char *Text) {
    if (gWrite != 0 && Text != 0) {
        gWrite(Text);
    }
}

static void WriteHex64(UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    WriteOut(Buf);
}

static void WriteHex2(UINT32 V) {
    char B[3];

    FormatHexadecimal(B, (int)sizeof(B), V, 2, 0, 0);
    WriteOut(B);
}

static void WriteHex4(UINT32 V) {
    char B[5];

    FormatHexadecimal(B, (int)sizeof(B), V, 4, 0, 0);
    WriteOut(B);
}

static const char *TitleOf(const DEVICE_NODE *Dev) {
    if (Dev->FriendlyName[0]) {
        return Dev->FriendlyName;
    }
    if (Dev->Name[0]) {
        return Dev->Name;
    }
    return "pci";
}

static void PrintNode(DEVICE_NODE *Dev, int Depth, int Verbose) {
    int i;

    for (i = 0; i < Depth * 2; i++) {
        WriteOut(" ");
    }
    WriteOut("[");
    WriteHex2(Dev->PciBus);
    WriteOut(":");
    WriteHex2(Dev->PciDev);
    WriteOut(".");
    WriteHex2(Dev->PciFn & 0x7u);
    WriteOut("] ");
    WriteOut(TitleOf(Dev));
    WriteOut("\n");
    if (Verbose) {
        WriteOut("      ");
        WriteHex4(Dev->Vendor);
        WriteOut(":");
        WriteHex4(Dev->Device);
        WriteOut("  class=");
        WriteHex2(Dev->Class);
        WriteOut("/");
        WriteHex2(Dev->Subclass);
        WriteOut("/");
        WriteHex2(Dev->ProgIf);
        WriteOut("  BAR0=");
        WriteHex64(Dev->Bar[0]);
        WriteOut("/");
        WriteHex64(Dev->BarSize[0]);
        WriteOut("  IRQ=");
        WriteHex2(Dev->Irq);
        WriteOut(Dev->IrqMode == 2 ? " mode=msix" : Dev->IrqMode == 1 ? " mode=msi" : " mode=intx");
        WriteOut("\n");
    }
}

static void PrintSubtree(DEVICE_NODE *Parent, int Depth, int Verbose) {
    DEVICE_NODE *Children[32];
    int N;
    int i;

    if (Depth >= 16) {
        DebugWrite("lsdev -t: depth >=16, stop\n");
        return;
    }
    N = DeviceGetChildren(Parent, Children, 32);
    for (i = 0; i < N; i++) {
        PrintNode(Children[i], Depth, Verbose);
        PrintSubtree(Children[i], Depth + 1, Verbose);
    }
}

void DeviceListDumpTree(int Verbose, void (*Write)(const char *Text)) {
    int i;
    int N;

    gWrite = Write;
    N = DeviceCount();

    WriteOut("=== Devices (tree) ===\n");
    for (i = 0; i < N; i++) {
        DEVICE_NODE *Dev = DeviceGet(i);

        if (!Dev || Dev->Parent) {
            continue;
        }
        PrintNode(Dev, 0, Verbose);
        PrintSubtree(Dev, 1, Verbose);
    }
}
