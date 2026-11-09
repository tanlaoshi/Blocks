/* SmpEncrypt.c — LE Start Encryption + Encryption Change 等待 */
#include "XHCI/Bluetooth/Private.h"
#define SMP_SLICE_MS 50u
#define HCI_OPCODE_LE_START_ENCRYPTION 0x2019u
#define HCI_EVENT_ENCRYPTION_CHANGE 0x08u
static void SmpEncryptPump(void) {
    if (HalCpuIsHypervisor()) {
        ProcessEvents();
    } else {
        ProcessEventsRealPc();
    }
    BluetoothHostPump();
    ServiceHidCompletions();
}
int BluetoothSmpWaitEncryptionChange(UINT16 Handle, UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        UINT8 Event[BLUETOOTH_EVENT_MAX];
        UINT32 Length = 0;
        UINT16 Got;
        SmpEncryptPump();
        while (BluetoothHostEventTake(Event, BLUETOOTH_EVENT_MAX, &Length)) {
            if (Length >= 6u && Event[0] == HCI_EVENT_ENCRYPTION_CHANGE) {
                Got = (UINT16)(Event[3] | ((UINT16)Event[4] << 8));
                Got = (UINT16)(Got & 0x0FFFu);
                if (Got == Handle) {
                    if (Event[2] != 0u || Event[5] == 0u) {
                        BootLogHex("bt: smp enc status=", Event[2], 2);
                        return -1;
                    }
                    return 0;
                }
            }
            Length = 0;
        }
        StallMilliseconds(SMP_SLICE_MS);
    }
    BootLog("bt: smp enc timeout\n");
    return -1;
}
int BluetoothSmpStartEncryption(UINT16 Handle, const UINT8 ShortTermKey[16]) {
    UINT8 Params[28];
    UINT32 Index;
    UINT64 Start;
    UINT64 Need;
    Params[0] = (UINT8)(Handle & 0xFFu);
    Params[1] = (UINT8)((Handle >> 8) & 0x0Fu);
    for (Index = 0; Index < 8u; Index++) {
        Params[2u + Index] = 0;
    }
    Params[10] = 0;
    Params[11] = 0;
    for (Index = 0; Index < 16u; Index++) {
        Params[12u + Index] = ShortTermKey[Index];
    }
    BluetoothHostEventQueueFlush();
    if (BluetoothHciSendRaw(HCI_OPCODE_LE_START_ENCRYPTION, Params, 28) < 0) {
        return -1;
    }
    Start = ReadTsc();
    Need = 2000ULL * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        UINT8 Event[BLUETOOTH_EVENT_MAX];
        UINT32 Length = 0;
        UINT16 Opcode;
        SmpEncryptPump();
        while (BluetoothHostEventTake(Event, BLUETOOTH_EVENT_MAX, &Length)) {
            if (Length >= 6u && Event[0] == HCI_EVENT_COMMAND_STATUS) {
                Opcode = (UINT16)(Event[4] | ((UINT16)Event[5] << 8));
                if (Opcode == HCI_OPCODE_LE_START_ENCRYPTION) {
                    if (Event[2] != 0u) {
                        BootLogHex("bt: smp Start Enc CS=", Event[2], 2);
                        return -1;
                    }
                    return BluetoothSmpWaitEncryptionChange(Handle, 10000u);
                }
            }
            Length = 0;
        }
        StallMilliseconds(SMP_SLICE_MS);
    }
    return -1;
}
