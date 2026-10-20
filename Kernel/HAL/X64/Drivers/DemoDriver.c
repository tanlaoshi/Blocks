/*
 * DemoDriver.c — 课堂 Demo（PR-D-tpl-2 / PR-DRV-match-demo）
 *
 * 证明 Register → Match → Probe(BusCtx) → Bind → lsdev 路径；
 * 不摸硬件、不抢 Input Backend。
 * 关闭：编译加 -DDEMO_DRIVER=0（见 HalDevices.c）。
 */
#include "Driver.h"
#include "DriverBoot.h"
#include "Device.h"
#include "Hal.h"
#include "Debug.h"

/*
 * PR-DRV-match-demo：填一张可重复命中的 PCI 表。
 * 匹配任意 Intel PCI 设备（Vendor=0x8086 精确，Device 通配）；
 * 枚举序首张即宿主桥（QEMU 0x8086:0x1237 / NUC），无人认领，作 demo 命中靶。
 * 表以 DRIVER_MATCH_NONE 结尾。
 */
static const DRIVER_MATCH gDemoMatches[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = 0x8086, .Device = 0,
                 .VendorMask = 0xFFFF, .DeviceMask = 0 } },
    { .Type = DRIVER_MATCH_NONE }
};

static int DemoProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    if (BusCtx) {
        const DEVICE_NODE *Dev = (const DEVICE_NODE *)BusCtx;
        DebugWrite("demo: probe BusCtx=");
        DebugHex32((UINT32)(UINTN)BusCtx);
        DebugWrite(" vid:did=");
        DebugHex32(((UINT32)Dev->Device << 16) | Dev->Vendor);
        DebugWrite("\n");
    } else {
        DebugWrite("demo: probe BusCtx=NULL\n");
    }
    return 0; /* 命中即绑；课堂证明 Match→BusCtx→Bind */
}

static int DemoBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    DebugWrite("demo: bind called\n");
    /*
     * 故意不调用 DriverInputAttach。
     * 只占 Driver 实例槽，使 lsdev 可见；真 Input 仍由 xhci-hid / ps2-kbd 管理。
     */
    return 0;
}

static void DemoRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const DRIVER gDemoDriver = {
    .Name = "demo-driver",
    .Class = DRIVER_CLASS_INPUT,
    .Match = gDemoMatches,
    .Probe = DemoProbe,
    .Bind = DemoBind,
    .Remove = DemoRemove,
};

void DemoDriverRegister(void) {
    (void)DriverRegister(&gDemoDriver);
}

#if DEMO_DRIVER
DRIVER_BOOT(DRIVER_BOOT_DEMO, DemoDriverRegister);
#endif
