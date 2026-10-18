/*
 * DeviceEnum.c — Arm64 把已登记的 virtio 设备写入设备表
 *
 * 不扫 PCI。探测仍由后面的 ProbeClass 做，这里只记节点。
 */
#include "Device.h"
#include "CString.h"

static void AddMmio(const char *Name, UINT16 VirtioId) {
    DEVICE_NODE Node = {0};

    Node.Bus = DEVICE_BUS_MMIO;
    Node.Device = VirtioId;
    StringCopy(Node.Name, sizeof(Node.Name), Name);
    StringCopy(Node.FriendlyName, sizeof(Node.FriendlyName), Name);
    (void)DeviceAdd(&Node);
}

void HalDeviceEnumerate(void) {
    AddMmio("virtio-blk", 2);
    AddMmio("virtio-input", 18);
    AddMmio("virtio-net", 1);
}
