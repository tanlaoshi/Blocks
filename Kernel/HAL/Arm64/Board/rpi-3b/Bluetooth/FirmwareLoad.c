/*
 * FirmwareLoad.c — 下发 BCM43430A1.hcd（对表 Linux btbcm_patchram）
 *
 * 序：Download_Minidriver(0xFC2E) → 50ms → 逐块发文件内 HCI 命令
 *     （多为 Write_RAM 0xFC4C，末条 Launch_RAM 0xFC4E）→ 250ms。
 * .hcd 记录格式 = 标准 HCI 命令头（opcode le16 + plen u8 + params），无 H4 类型字节。
 */
#include "BluetoothPrivate.h"
#include "Hal.h"
#include "HalDriverFile.h"
#include "HalSerial.h"
#include "PhysicalMemory.h"

static int PiBluetoothReadFirmwareBlob(UINT8 **OutBlob, UINTN *OutBytes) {
    UINT8 Probe[16];
    UINTN Size = 0;
    UINTN Got = 0;
    UINT32 Pages;
    void *Memory;
    int Error;

    *OutBlob = 0;
    *OutBytes = 0;
    Error = HalDriverFileRead(PI_BT_FIRMWARE_PATH, Probe, sizeof(Probe), &Size);
    if (Error != 0 || Size < 4u) {
        return -1;
    }
    Pages = (PI_BT_FIRMWARE_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
    Memory = PhysicalMemoryAllocatePages(Pages);
    if (Memory == 0) {
        return -1;
    }
    Error = HalDriverFileRead(PI_BT_FIRMWARE_PATH, Memory, Pages * PAGE_SIZE, &Got);
    if (Error != 0 || Got < 64u || Got > PI_BT_FIRMWARE_MAX_BYTES) {
        PhysicalMemoryFreePages(Memory, Pages);
        return -1;
    }
    *OutBlob = (UINT8 *)Memory;
    *OutBytes = Got;
    return 0;
}

int PiBluetoothFirmwareLoad(void) {
    UINT8 *Blob = 0;
    UINTN Bytes = 0;
    UINTN Offset = 0;
    UINT8 Status = 0xFFu;
    UINT32 Pages;
    UINT16 Opcode;
    UINT8 ParameterLength;
    UINT32 CommandCount = 0u;

    if (PiBluetoothReadFirmwareBlob(&Blob, &Bytes) != 0) {
        HalSerialBootMarkForce("Boot: Bluetooth .hcd read fail\n");
        return -1;
    }

    if (PiHciSendCommandWithTimeout(PI_HCI_OPCODE_DOWNLOAD_MINIDRIVER, 0, 0u, &Status,
                                    0, 0u, 0,
                                    PI_BT_FIRMWARE_COMMAND_TIMEOUT_MS) != 0 ||
        Status != 0u) {
        HalSerialBootMarkForce("Boot: Bluetooth Download Minidriver fail\n");
        Pages = (PI_BT_FIRMWARE_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
        PhysicalMemoryFreePages(Blob, Pages);
        return -2;
    }
    HalDelayMilliseconds(50u);

    while (Offset + 3u <= Bytes) {
        Opcode = (UINT16)(Blob[Offset] | ((UINT16)Blob[Offset + 1u] << 8));
        ParameterLength = Blob[Offset + 2u];
        Offset += 3u;
        if (Offset + ParameterLength > Bytes) {
            HalSerialBootMarkForce("Boot: Bluetooth .hcd truncated\n");
            Pages = (PI_BT_FIRMWARE_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
            PhysicalMemoryFreePages(Blob, Pages);
            return -3;
        }
        if (PiHciSendCommandWithTimeout(Opcode, &Blob[Offset], ParameterLength, &Status,
                                        0, 0u, 0,
                                        PI_BT_FIRMWARE_COMMAND_TIMEOUT_MS) != 0 ||
            Status != 0u) {
            HalSerialBootMarkForce("Boot: Bluetooth .hcd command fail\n");
            Pages = (PI_BT_FIRMWARE_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
            PhysicalMemoryFreePages(Blob, Pages);
            return -4;
        }
        Offset += ParameterLength;
        CommandCount++;
    }

    Pages = (PI_BT_FIRMWARE_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
    PhysicalMemoryFreePages(Blob, Pages);
    HalDelayMilliseconds(250u);
    HalSerialBootMarkForce("Boot: Bluetooth .hcd ok\n");
    (void)CommandCount;
    return 0;
}
