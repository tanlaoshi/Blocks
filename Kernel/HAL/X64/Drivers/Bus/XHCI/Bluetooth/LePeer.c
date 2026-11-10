/* LePeer.c — LE 连接句柄与对端地址（SMP/ATT/重连共用；PR-BTM-10 保留上次对端） */
#include "XHCI/Bluetooth/Private.h"

static UINT16 sLeConnectionHandle;
static int sLeConnected;
static UINT8 sLePeerAddress[6];
static UINT8 sLePeerAddressType;
static UINT8 sLeLastPeerAddress[6];
static UINT8 sLeLastPeerAddressType;
static int sLeLastPeerValid;

void BluetoothLeConnectionStateClear(void) {
    sLeConnected = 0;
    sLeConnectionHandle = 0;
    /* 上次对端地址保留，供同会话 bt mouse 无参重连 */
}

void BluetoothLeConnectionStateSet(UINT16 Handle, const UINT8 Address[6],
                                   UINT8 AddressType) {
    UINT32 Byte;

    sLeConnectionHandle = Handle;
    sLePeerAddressType = AddressType;
    for (Byte = 0; Byte < 6u; Byte++) {
        sLePeerAddress[Byte] = Address ? Address[Byte] : 0;
        sLeLastPeerAddress[Byte] = sLePeerAddress[Byte];
    }
    sLeLastPeerAddressType = AddressType;
    sLeLastPeerValid = 1;
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

int BluetoothLeLastPeerAddressGet(UINT8 Out[6], UINT8 *AddressTypeOut) {
    UINT32 Byte;

    if (Out == 0 || !sLeLastPeerValid) {
        return -1;
    }
    for (Byte = 0; Byte < 6u; Byte++) {
        Out[Byte] = sLeLastPeerAddress[Byte];
    }
    if (AddressTypeOut != 0) {
        *AddressTypeOut = sLeLastPeerAddressType;
    }
    return 0;
}

int BluetoothLeIsConnected(void) {
    return sLeConnected ? 1 : 0;
}
