/*
 * Match.c — 驱动匹配表过滤（PR-DRV-match-logic）
 *
 * DriverMatchesDevice：按 DRIVER_MATCH 表过滤 DEVICE_NODE。
 * Match==NULL 由 DriverProbeClass 走旧路径（Probe 自扫），本文件只管有表路径。
 * 规格见 Documents/待做/设备管理器驱动匹配.md。
 */
#include "Driver.h"
#include "CString.h"
#include "Device.h"


/* 掩码 0 表示不比较。Class 三项与 VID/DID 同一规则。 */
static int ByteOk(UINT8 Have, UINT8 Want, UINT8 Mask) {
    if (Mask == 0) {
        return 1;
    }
    return (Have & Mask) == (Want & Mask);
}

/*
 * 命中规则（§3）：
 *   PCI ：VID/DID 按掩码比较，Mask=0 即通配。
 *         Class/Subclass/ProgIf 同样：掩码 0 忽略，0xFF 整字节比较。
 *   DTB ：Compatible 串全等。
 *   FIXED：Bar[0]==entry.Base。
 * 表以 DRIVER_MATCH_NONE 结尾；任一条命中即返回 1。
 */
int DriverMatchesDevice(const DRIVER *Drv, const DEVICE_NODE *Dev) {
    const DRIVER_MATCH *M;

    if (!Drv || !Dev || !Drv->Match) {
        return 0;
    }
    for (M = Drv->Match; M->Type != DRIVER_MATCH_NONE; M++) {
        if (M->Type == DRIVER_MATCH_PCI) {
            if (Dev->Bus != DEVICE_BUS_PCI) continue;
            if ((Dev->Vendor & M->U.Pci.VendorMask) !=
                (M->U.Pci.Vendor & M->U.Pci.VendorMask)) continue;
            if ((Dev->Device & M->U.Pci.DeviceMask) !=
                (M->U.Pci.Device & M->U.Pci.DeviceMask)) continue;
            if (!ByteOk(Dev->Class, M->U.Pci.Class, M->U.Pci.ClassMask)) continue;
            if (!ByteOk(Dev->Subclass, M->U.Pci.Subclass, M->U.Pci.SubclassMask)) {
                continue;
            }
            if (!ByteOk(Dev->ProgIf, M->U.Pci.ProgIf, M->U.Pci.ProgIfMask)) continue;
            return 1;
        } else if (M->Type == DRIVER_MATCH_DTB) {
            if (Dev->Bus != DEVICE_BUS_DTB) continue;
            if (StringEqual(Dev->Compatible, M->U.Dtb.Compatible)) return 1;
        } else if (M->Type == DRIVER_MATCH_FIXED) {
            if (Dev->Bus != DEVICE_BUS_FIXED) continue;
            if (Dev->Bar[0] == M->U.Fixed.Base) return 1;
        }
    }
    return 0;
}
