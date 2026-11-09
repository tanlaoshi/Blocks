/*
 * Bluetooth.c — 壳 bt / info / scan / visible / hide（btx-3…5）
 *
 * Pi 诊断符号（BoardBluetoothDiagnosis / PiUartTransport*）仅 rpi-3b 提供。
 * 用 weak：CI `build.sh arm64`（BOARD=virt）链得过；Pi 真机仍打快照。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalBluetooth.h"

static void WriteHexByte(UINT8 Value) {
    static const char Digits[] = "0123456789ABCDEF";
    char Hex[3];

    Hex[0] = Digits[(Value >> 4) & 0x0Fu];
    Hex[1] = Digits[Value & 0x0Fu];
    Hex[2] = 0;
    gConsole.Write(Hex);
}

static void WriteAddress(const UINT8 Address[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
    int Index;

    for (Index = 5; Index >= 0; Index--) {
        WriteHexByte(Address[Index]);
        if (Index > 0) {
            gConsole.Write(":");
        }
    }
}

static int ArgEqualIgnoreCase(const char *Arg, const char *Word) {
    const char *A = Word;
    const char *B = Arg;

    if (!B) {
        return 0;
    }
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;

        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void WriteBringUpStatus(void) {
    int ProbeDone = 0;
    int UsbSeen = 0;
    int HciResetOk = 0;
    UINT16 VendorId = 0;
    UINT16 ProductId = 0;
    UINT32 RootPort = 0;

    HalBluetoothBringUpStatus(&ProbeDone, &UsbSeen, &HciResetOk, &VendorId,
                              &ProductId, &RootPort);
    gConsole.Write("bt: not ready probed=");
    gConsole.Write(ProbeDone ? "1" : "0");
    gConsole.Write(" seen=");
    gConsole.Write(UsbSeen ? "1" : "0");
    gConsole.Write(" reset=");
    gConsole.Write(HciResetOk ? "1" : "0");
    gConsole.Write(" vid=");
    gConsole.WriteHex32(VendorId);
    gConsole.Write(" pid=");
    gConsole.WriteHex32(ProductId);
    gConsole.Write(" port=");
    gConsole.WriteHex32(RootPort);
    if (UsbSeen && !HciResetOk) {
        gConsole.Write(" (Intel fw/Reset fail; bt init; else cold power)\n");
    } else {
        gConsole.Write(" (bt init to retry)\n");
    }
}

static void CommandBluetoothInfo(void) {
    UINT8 Addr[HAL_BLUETOOTH_ADDRESS_LENGTH];
    char Name[HAL_BLUETOOTH_NAME_MAX];
    int ProbeDone = 0;
    int UsbSeen = 0;

    /* AnnounceBootReady 早于 Worker HCI；先等首探再报 */
    if (!HalBluetoothReady()) {
        (void)HalBluetoothWaitReady(5000u);
    }
    /*
     * seen 但 reset=0：Worker 首探灌固件失败。裸 bt 再探一次（等同 bt init），
     * 仍挂则多半软 reboot 后 Intel Interrupt 哑火 → 须断电冷启。
     */
    if (!HalBluetoothReady()) {
        HalBluetoothBringUpStatus(&ProbeDone, &UsbSeen, 0, 0, 0, 0);
        if (ProbeDone && UsbSeen) {
            gConsole.Write("bt: retry init (seen, reset fail)...\n");
            (void)HalBluetoothInitialize();
        }
    }
    if (!HalBluetoothReady()) {
        WriteBringUpStatus();
        return;
    }
    if (HalBluetoothGetDeviceAddress(Addr) != 0) {
        gConsole.Write("bt: addr fail\n");
        return;
    }
    Name[0] = 0;
    (void)HalBluetoothGetLocalName(Name, sizeof(Name));
    gConsole.Write("bt: ready name=");
    gConsole.Write(Name[0] ? Name : "(none)");
    gConsole.Write(" addr=");
    WriteAddress(Addr);
    gConsole.Write(HalBluetoothDiscoverable() ? " visible\n" : " hidden\n");
}

static void CommandBluetoothScan(void) {
    HAL_BLUETOOTH_SCAN_ENTRY Table[HAL_BLUETOOTH_SCAN_MAX];
    int N;
    int I;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    gConsole.Write("bt: scanning (~8s)...\n");
    N = HalBluetoothScan(Table, HAL_BLUETOOTH_SCAN_MAX, 0);
    if (N < 0) {
        gConsole.Write("bt: scan fail\n");
        return;
    }
    if (N == 0) {
        gConsole.Write("bt: no devices (phone BT on?)\n");
        return;
    }
    for (I = 0; I < N; I++) {
        char Line[8];
        int D = I + 1;

        gConsole.Write("bt: ");
        Line[0] = (char)('0' + (D / 10));
        Line[1] = (char)('0' + (D % 10));
        Line[2] = 0;
        if (D < 10) {
            gConsole.Write(&Line[1]);
        } else {
            gConsole.Write(Line);
        }
        gConsole.Write(" ");
        WriteAddress(Table[I].Address);
        gConsole.Write(" ");
        gConsole.Write(Table[I].Name[0] ? Table[I].Name : "(no name)");
        gConsole.Write("\n");
    }
}

static void CommandBluetoothVisible(int On) {
    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    if (HalBluetoothSetDiscoverable(On) != 0) {
        gConsole.Write(On ? "bt: visible fail\n" : "bt: hide fail\n");
        return;
    }
    gConsole.Write(On ? "bt: visible (phone search Blocks)\n"
                      : "bt: hidden\n");
}

const char *BoardBluetoothDiagnosis(void) __attribute__((weak));
void PiUartTransportSnapshot(UINT32 Out[6]) __attribute__((weak));
UINT32 PiUartTransportClockHz(void) __attribute__((weak));

static void CommandBluetoothInitialize(void) {
    int Rc = HalBluetoothInitialize();

    if (BoardBluetoothDiagnosis != 0) {
        gConsole.Write("bt: diag=");
        gConsole.Write(BoardBluetoothDiagnosis());
        gConsole.Write("\n");
    }
    /* Pi：附加 PL011 分频快照（Duo 无 PiUartTransport*，弱符为空则跳过） */
    if (PiUartTransportSnapshot != 0 && PiUartTransportClockHz != 0) {
        UINT32 Snapshot[6] = {0u, 0u, 0u, 0u, 0u, 0u};

        PiUartTransportSnapshot(Snapshot);
        gConsole.Write("bt: uart clk=");
        gConsole.WriteHex32(PiUartTransportClockHz());
        gConsole.Write(" ibrd/fbrd=");
        gConsole.WriteHex32(Snapshot[3]);
        gConsole.Write("/");
        gConsole.WriteHex32(Snapshot[4]);
        gConsole.Write(" cr/fr/lcrh=");
        gConsole.WriteHex32(Snapshot[0]);
        gConsole.Write("/");
        gConsole.WriteHex32(Snapshot[1]);
        gConsole.Write("/");
        gConsole.WriteHex32(Snapshot[2]);
        gConsole.Write(" gpfsel3=");
        gConsole.WriteHex32(Snapshot[5]);
        gConsole.Write("\n");
    }

    gConsole.Write("bt: init rc=");
    gConsole.WriteHex32((UINT32)Rc);
    gConsole.Write(Rc == 1 ? " (bring-up ok)" : " (fail)");
    if (HalBluetoothReady()) {
        gConsole.Write(", ready\n");
    } else {
        gConsole.Write("\n");
        WriteBringUpStatus();
    }
}

static void CommandBluetoothAclTest(void) {
    UINT16 PacketLength = 0;
    UINT16 PacketsFree = 0;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    if (HalBluetoothAclCreditQuery(&PacketLength, &PacketsFree) != 0) {
        gConsole.Write("bt: acl credit fail\n");
        return;
    }
    gConsole.Write("bt: acl mtu=");
    gConsole.WriteHex32(PacketLength);
    gConsole.Write(" credit=");
    gConsole.WriteHex32(PacketsFree);
    gConsole.Write("\n");
    if (HalBluetoothAclOutTest() != 0) {
        gConsole.Write("bt: acltest fail\n");
        return;
    }
    if (HalBluetoothAclCreditQuery(&PacketLength, &PacketsFree) == 0) {
        gConsole.Write("bt: acl after credit=");
        gConsole.WriteHex32(PacketsFree);
        gConsole.Write("\n");
    }
    gConsole.Write("bt: acltest ok\n");
}

static void CommandBluetooth(int Argc, char **Argv) {
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "init")) {
        CommandBluetoothInitialize();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "scan")) {
        CommandBluetoothScan();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "visible")) {
        CommandBluetoothVisible(1);
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "hide")) {
        CommandBluetoothVisible(0);
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "acltest")) {
        CommandBluetoothAclTest();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "lescan")) {
        ShellBluetoothLeScan();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "connect")) {
        ShellBluetoothLeConnect(Argc, Argv);
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "disconnect")) {
        ShellBluetoothLeDisconnect();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "smp")) {
        ShellBluetoothSmpJustWorks();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "hogp")) {
        ShellBluetoothAttHidDiscover();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "notify")) {
        ShellBluetoothAttHidSubscribe();
        return;
    }
    CommandBluetoothInfo();
}

void ShellCommandsBluetoothRegister(void) {
    gConsole.Register(
        "bt",
        "bt [info|init|scan|lescan|connect|smp|hogp|notify|disconnect|visible|hide|acltest]",
        CommandBluetooth);
}
