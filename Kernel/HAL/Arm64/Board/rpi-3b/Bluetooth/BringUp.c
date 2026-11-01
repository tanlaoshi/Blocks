/*
 * BringUp.c — Pi 3B 蓝牙 bring-up（bt-pi-1 只到 HCI_Reset）
 *
 * 本刀判据（Docs/24 § bt-pi-1）：
 *   串口出现 `Boot: Bluetooth UART0 up` → `HCI Reset ok`；
 *   壳仍走 mini-UART；HalBluetoothReady() 仍为 0（到 bt-pi-2 才置）。
 *
 * 序（Cursor 接手 · 一次一变量已合并为固定序，均有对表）：
 *   A GPCLK2(GPIO43) → B BT_ON(128) → UART0 → HCI_Reset
 *   不加载 .hcd（bt-pi-2）；不扫波特率。
 */
#include "BluetoothPrivate.h"
#include "Hal.h"
#include "HalSerial.h"

static const char *sPiBluetoothDiagnosis = "not run";
static UINT32 sPiBluetoothAttemptMask;

const char *BoardBluetoothDiagnosis(void) {
    return sPiBluetoothDiagnosis;
}

UINT32 BoardBluetoothAttemptMask(void) {
    return sPiBluetoothAttemptMask;
}

static void PiBluetoothDrainReceive(void) {
    UINT8 Byte;
    UINT32 Guard = 64u;

    while (Guard-- > 0u && PiUartReadByte(&Byte, 1u) == 0) {
    }
}

int BoardBluetoothBringUpReset(void) {
    UINT8 Status = 0xFFu;

    sPiBluetoothAttemptMask = 0u;
    (void)PiBluetoothClock2Enable32k();
    (void)PiBluetoothPowerEnable();
    (void)PiUartTransportInitialize();
    HalSerialBootMarkForce("Boot: Bluetooth UART0 up\n");
    PiBluetoothDrainReceive();
    if (PiHciSendCommand(PI_HCI_OPCODE_RESET, 0, 0u, &Status) != 0) {
        sPiBluetoothDiagnosis = "no response";
        HalSerialBootMarkForce("Boot: Bluetooth HCI Reset no response\n");
        return -1;
    }
    if (Status != 0u) {
        sPiBluetoothDiagnosis = "command complete but status!=0";
        HalSerialBootMarkForce("Boot: Bluetooth HCI Reset failed (status)\n");
        return -2;
    }
    sPiBluetoothDiagnosis = "HCI Reset ok";
    HalSerialBootMarkForce("Boot: Bluetooth HCI Reset ok\n");
    return 0;
}

int BoardBluetoothSelfTest(void) {
    return BoardBluetoothBringUpReset();
}
