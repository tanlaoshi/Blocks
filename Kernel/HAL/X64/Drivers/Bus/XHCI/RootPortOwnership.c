/*
 * RootPortOwnership.c — 根口「已有主人」门闩（usb-hid-keep / 保盘）
 *
 * 凡会 ResetPort / AddressDeviceOnPort 的旁路认领（鼠 / Wifi / BT / FTDI / CDC）
 * 必须先问本函数；MSC 已认或 boot-watch 钉住的口一律视为占用。
 * 反例：MouseDeferred 在无独立鼠时扫根口，踩启动 U 盘 → bulk cc=0x04 → boot-gone。
 */
#include "XHCI/Private.h"

int XhciRootPortIsOwned(UINT32 Port) {
    if (Port == 0 || Port > gMaxPorts) {
        return 1;
    }
    if (gSlotId != 0 && Port == gPort1) {
        return 1;
    }
    if (gMouseSlotId != 0 && gMouseHubSlot == 0 && Port == gMousePort) {
        return 1;
    }
    if (gHubSlotId != 0 && Port == gHubRootPort) {
        return 1;
    }
    if (gMscPort != 0 && Port == gMscPort) {
        return 1;
    }
    if (gMscBootWatchPort != 0 && Port == gMscBootWatchPort) {
        return 1;
    }
    if (gFtdiClaimed && Port == gFtdiPort) {
        return 1;
    }
    if (gCdcClaimed && Port == gCdcPort) {
        return 1;
    }
    if (XhciBluetoothSeen() && Port == XhciBluetoothRootPort()) {
        return 1;
    }
    return 0;
}
