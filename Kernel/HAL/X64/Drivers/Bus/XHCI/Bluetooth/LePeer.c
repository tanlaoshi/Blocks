/* LePeer.c — LE 连接句柄与对端地址（SMP/ATT 共用） */
#include "XHCI/Bluetooth/Private.h"
static UINT16 sLeConnectionHandle;
static int sLeConnected;
static UINT8 sLePeerAddress[6];
static UINT8 sLePeerAddressType;
void BluetoothLeConnectionStateClear(void) {
    sLeConnected = 0;
    sLeConnectionHandle = 0;
    sLePeerAddressType = 0;
}
void BluetoothLeConnectionStateSet(UINT16 Handle, const UINT8 Address[6],
                                   UINT8 AddressType) {
    UINT32 Byte;
    sLeConnectionHandle = Handle;
    sLePeerAddressType = AddressType;
    for (Byte = 0; Byte < 6u; Byte++) {
        sLePeerAddress[Byte] = Address ? Address[Byte] : 0;
    }
    sLeConnected = 1;
}
int BluetoothLeConnectionHandleGet(UINT16 *Out) {
    if (Out == 0 || !sLeConnected || sLeConnectionHandle == 0u) {
        if (Out != 0) {
            *Out = 0;
        }
        return -1;
    }
    *Out = sLeConnectionHandle;
    return 0;
}
int BluetoothLePeerAddressGet(UINT8 Out[6], UINT8 *AddressTypeOut) {
    UINT32 Byte;
    if (Out == 0 || !sLeConnected) {
        return -1;
    }
    for (Byte = 0; Byte < 6u; Byte++) {
        Out[Byte] = sLePeerAddress[Byte];
    }
    if (AddressTypeOut != 0) {
        *AddressTypeOut = sLePeerAddressType;
    }
    return 0;
}
int BluetoothLeIsConnected(void) {
    return sLeConnected ? 1 : 0;
}
