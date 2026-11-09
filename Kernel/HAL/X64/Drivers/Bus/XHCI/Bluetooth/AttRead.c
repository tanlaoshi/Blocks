/*
 * AttRead.c — ATT Read / Read Blob（Report Map 等长属性）
 */
#include "XHCI/Bluetooth/Private.h"
#define ATT_READ_REQUEST 0x0Au
#define ATT_READ_RESPONSE 0x0Bu
#define ATT_READ_BLOB_REQUEST 0x0Cu
#define ATT_READ_BLOB_RESPONSE 0x0Du
int BluetoothAttReadRequest(UINT16 ConnectionHandle, UINT16 AttributeHandle,
                            UINT8 *ValueOut, UINT16 ValueCapacity,
                            UINT16 *ValueLengthOut, UINT32 TimeoutMs,
                            UINT16 Stage) {
    UINT8 Request[3];
    UINT8 Response[48];
    UINT16 ResponseLength = 0;
    UINT16 Index;
    UINT16 CopyLength;
    if (ValueOut == 0 || ValueCapacity == 0u) {
        return -1;
    }
    Request[0] = ATT_READ_REQUEST;
    Request[1] = (UINT8)(AttributeHandle & 0xFFu);
    Request[2] = (UINT8)((AttributeHandle >> 8) & 0xFFu);
    if (BluetoothAttSend(ConnectionHandle, Request, 3) < 0) {
        return -1;
    }
    if (BluetoothAttExpect(ATT_READ_RESPONSE, Response, sizeof(Response),
                           &ResponseLength, TimeoutMs, Stage) < 0 ||
        ResponseLength < 1u) {
        return -1;
    }
    CopyLength = (UINT16)(ResponseLength - 1u);
    if (CopyLength > ValueCapacity) {
        CopyLength = ValueCapacity;
    }
    for (Index = 0; Index < CopyLength; Index++) {
        ValueOut[Index] = Response[1u + Index];
    }
    if (ValueLengthOut != 0) {
        *ValueLengthOut = CopyLength;
    }
    return 0;
}
int BluetoothAttReadBlobRequest(UINT16 ConnectionHandle, UINT16 AttributeHandle,
                                UINT16 ValueOffset, UINT8 *ValueOut,
                                UINT16 ValueCapacity, UINT16 *ValueLengthOut,
                                UINT32 TimeoutMs, UINT16 Stage) {
    UINT8 Request[5];
    UINT8 Response[48];
    UINT16 ResponseLength = 0;
    UINT16 Index;
    UINT16 CopyLength;
    if (ValueOut == 0 || ValueCapacity == 0u) {
        return -1;
    }
    Request[0] = ATT_READ_BLOB_REQUEST;
    Request[1] = (UINT8)(AttributeHandle & 0xFFu);
    Request[2] = (UINT8)((AttributeHandle >> 8) & 0xFFu);
    Request[3] = (UINT8)(ValueOffset & 0xFFu);
    Request[4] = (UINT8)((ValueOffset >> 8) & 0xFFu);
    if (BluetoothAttSend(ConnectionHandle, Request, 5) < 0) {
        return -1;
    }
    if (BluetoothAttExpect(ATT_READ_BLOB_RESPONSE, Response, sizeof(Response),
                           &ResponseLength, TimeoutMs, Stage) < 0 ||
        ResponseLength < 1u) {
        return -1;
    }
    CopyLength = (UINT16)(ResponseLength - 1u);
    if (CopyLength > ValueCapacity) {
        CopyLength = ValueCapacity;
    }
    for (Index = 0; Index < CopyLength; Index++) {
        ValueOut[Index] = Response[1u + Index];
    }
    if (ValueLengthOut != 0) {
        *ValueLengthOut = CopyLength;
    }
    return 0;
}
int BluetoothAttReadLong(UINT16 ConnectionHandle, UINT16 AttributeHandle,
                         UINT8 *ValueOut, UINT16 ValueCapacity,
                         UINT16 *ValueLengthOut, UINT32 TimeoutMs,
                         UINT16 Stage) {
    UINT16 Total = 0;
    UINT16 Chunk = 0;
    UINT16 MaximumTransmissionUnit;
    UINT16 MaxChunk;
    if (ValueOut == 0 || ValueCapacity == 0u) {
        return -1;
    }
    if (BluetoothAttReadRequest(ConnectionHandle, AttributeHandle, ValueOut,
                                ValueCapacity, &Chunk, TimeoutMs, Stage) < 0) {
        return -1;
    }
    Total = Chunk;
    MaximumTransmissionUnit = BluetoothAttMaximumTransmissionUnitGet();
    /* Read/Blob 响应体上限 ≈ MTU-1 */
    MaxChunk = (MaximumTransmissionUnit > 1u)
                   ? (UINT16)(MaximumTransmissionUnit - 1u)
                   : 22u;
    while (Chunk >= MaxChunk && Total < ValueCapacity) {
        UINT16 Remain = (UINT16)(ValueCapacity - Total);
        Chunk = 0;
        if (BluetoothAttReadBlobRequest(ConnectionHandle, AttributeHandle, Total,
                                        ValueOut + Total, Remain, &Chunk,
                                        TimeoutMs, Stage) < 0) {
            break;
        }
        if (Chunk == 0u) {
            break;
        }
        Total = (UINT16)(Total + Chunk);
        BluetoothRequeueHid();
    }
    if (ValueLengthOut != 0) {
        *ValueLengthOut = Total;
    }
    return Total > 0u ? 0 : -1;
}
