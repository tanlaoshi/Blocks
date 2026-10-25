/*
 * DeviceEnum.c — RiscV 把已登记的 virtio 设备写入设备表
 *
 * 仍看 BOARD_HAS_*。不扫 PCI。探测顺序不在这里改。
 */
#include "Device.h"
#include "CString.h"
#include "BoardConfig.h"

#if BOARD_HAS_BLOCK || BOARD_HAS_FRAMEBUFFER || BOARD_HAS_NET
static void AddMmio(const char *Name, UINT16 VirtioId) {
    DEVICE_NODE Node = {0};

    Node.Bus = DEVICE_BUS_MMIO;
    Node.Device = VirtioId;
    StringCopy(Node.Name, sizeof(Node.Name), Name);
    StringCopy(Node.FriendlyName, sizeof(Node.FriendlyName), Name);
    (void)DeviceAdd(&Node);
}
#endif

void HalDeviceEnumerate(void) {
#if BOARD_HAS_BLOCK
    AddMmio("virtio-blk", 2);
#endif
#if BOARD_HAS_FRAMEBUFFER
    AddMmio("virtio-input", 18);
#endif
#if BOARD_HAS_NET
    AddMmio("virtio-net", 1);
#endif
}
