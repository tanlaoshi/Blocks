/*
 * IntelFirmwareSecureSend.c — 0xFC09 分片 + RSA 头 + .sfi 载荷（对表 btintel）
 *
 * 8087:0A2B / WsP：RSA_HEADER 644 → Data 碎片（4 字节对齐）。
 */
#include "XHCI/Bluetooth/Private.h"

#define INTEL_RSA_HEADER_LENGTH 644u
#define INTEL_SECURE_SEND_FRAGMENT_MAX 252u
/* 对表 HCI_INIT_TIMEOUT；U 盘读 .sfi 后首片 0xFC09 曾 2s 无事件 */
#define INTEL_SECURE_SEND_TIMEOUT_MS 10000u

int BluetoothIntelSecureSend(UINT8 FragmentType, const UINT8 *Data,
                             UINT32 Length) {
    UINT8 Params[1u + INTEL_SECURE_SEND_FRAGMENT_MAX];
    UINT32 Remaining = Length;
    const UINT8 *Cursor = Data;
    UINT8 Status = 0xFFu;
    UINT8 Chunk;

    if (Data == 0 && Length != 0u) {
        return -1;
    }
    while (Remaining > 0u) {
        Chunk = (Remaining > INTEL_SECURE_SEND_FRAGMENT_MAX)
                    ? (UINT8)INTEL_SECURE_SEND_FRAGMENT_MAX
                    : (UINT8)Remaining;
        Params[0] = FragmentType;
        {
            UINT8 I;

            for (I = 0; I < Chunk; I++) {
                Params[1u + I] = Cursor[I];
            }
        }
        if (BluetoothHciCommandTimeout(HCI_OPCODE_INTEL_SECURE_SEND, Params,
                                       (UINT8)(1u + Chunk), &Status, 0, 0, 0,
                                       INTEL_SECURE_SEND_TIMEOUT_MS) < 0 ||
            Status != 0u) {
            BootLogHex("Boot: Bluetooth Intel secure-send fail type=",
                       FragmentType, 2);
            BootLogHex(" status=", Status, 2);
            return -1;
        }
        Cursor += Chunk;
        Remaining -= Chunk;
    }
    return 0;
}

int BluetoothIntelSecureSendRsaHeader(const UINT8 *Firmware,
                                      UINTN FirmwareBytes) {
    if (Firmware == 0 || FirmwareBytes < INTEL_RSA_HEADER_LENGTH) {
        return -1;
    }
    /* Init CSS 128 + PKey 256 + Sign 256（对表 btintel_sfi_rsa_header_secure_send） */
    if (BluetoothIntelSecureSend(0x00u, Firmware, 128u) < 0) {
        return -1;
    }
    if (BluetoothIntelSecureSend(0x03u, Firmware + 128u, 256u) < 0) {
        return -1;
    }
    if (BluetoothIntelSecureSend(0x02u, Firmware + 388u, 256u) < 0) {
        return -1;
    }
    return 0;
}

int BluetoothIntelSecureSendPayload(const UINT8 *Firmware,
                                    UINTN FirmwareBytes) {
    UINTN Offset = INTEL_RSA_HEADER_LENGTH;
    UINT32 FragmentLength = 0u;
    UINT32 FragmentCount = 0u;

    if (Firmware == 0 || FirmwareBytes < INTEL_RSA_HEADER_LENGTH) {
        return -1;
    }
    while (Offset < FirmwareBytes) {
        UINT16 Opcode;
        UINT8 ParameterLength;
        UINT32 CommandBytes;

        if (Offset + 3u > FirmwareBytes) {
            BootLog("Boot: Bluetooth Intel .sfi truncated\n");
            return -1;
        }
        Opcode = (UINT16)(Firmware[Offset + FragmentLength] |
                          ((UINT16)Firmware[Offset + FragmentLength + 1u] << 8));
        ParameterLength = Firmware[Offset + FragmentLength + 2u];
        CommandBytes = 3u + (UINT32)ParameterLength;
        if (Offset + FragmentLength + CommandBytes > FirmwareBytes) {
            BootLog("Boot: Bluetooth Intel .sfi command overrun\n");
            return -1;
        }
        (void)Opcode;
        FragmentLength += CommandBytes;
        if ((FragmentLength % 4u) != 0u) {
            continue;
        }
        if (BluetoothIntelSecureSend(0x01u, Firmware + Offset,
                                     FragmentLength) < 0) {
            return -1;
        }
        FragmentCount++;
        if ((FragmentCount & 0xFFu) == 0u) {
            BootLogHex("Boot: Bluetooth Intel fw fragments=", FragmentCount, 4);
        }
        Offset += FragmentLength;
        FragmentLength = 0u;
    }
    if (FragmentLength != 0u) {
        BootLog("Boot: Bluetooth Intel .sfi align leftover\n");
        return -1;
    }
    BootLogHex("Boot: Bluetooth Intel fw fragments done=", FragmentCount, 4);
    return 0;
}
