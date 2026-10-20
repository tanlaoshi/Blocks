/*
 * Driver.h — 简单驱动框架公开面（PR-D1）
 * Common 只依赖本头；硬件细节留在 HAL/<Arch>/Drivers。
 */
#ifndef DRIVER_H
#define DRIVER_H

#include "BootTypes.h"
/* DEVICE_NODE 用于 DriverMatchesDevice 声明；Device.h 不回 include 本头，无环。 */
#include "Device.h"

#define DRIVER_MAX_DRIVERS  20
#define DRIVER_MAX_INSTANCES 20

typedef enum {
    DRIVER_CLASS_NONE = 0,
    DRIVER_CLASS_BLOCK,
    DRIVER_CLASS_INPUT,
    DRIVER_CLASS_NET,
    DRIVER_CLASS_DISPLAY,
    DRIVER_CLASS_AUDIO
} DRIVER_CLASS;

typedef struct DRIVER_INSTANCE DRIVER_INSTANCE;

/*
 * 驱动匹配表（阶段 2）。NULL = 旧行为（Probe 自扫 PCI/DTB）。
 * 表以 DRIVER_MATCH_NONE 结尾；框架按 Type 过滤后把 DEVICE_NODE * 经 BusCtx 传入 Probe。
 */
typedef enum {
    DRIVER_MATCH_NONE = 0,
    DRIVER_MATCH_PCI,
    DRIVER_MATCH_DTB,
    DRIVER_MATCH_FIXED
} DRIVER_MATCH_TYPE;

typedef struct DRIVER_MATCH {
    DRIVER_MATCH_TYPE Type;
    union {
        struct {
            UINT16 Vendor;
            UINT16 Device;
            UINT16 VendorMask;
            UINT16 DeviceMask;
            /* 掩码 0：不比较该字段（旧表零初始化即此）。0xFF：整字节比较，值可以是 0。 */
            UINT8 Class;
            UINT8 Subclass;
            UINT8 ProgIf;
            UINT8 ClassMask;
            UINT8 SubclassMask;
            UINT8 ProgIfMask;
        } Pci;
        struct {
            char Compatible[64];
        } Dtb;
        struct {
            UINT64 Base;
        } Fixed;
    } U;
} DRIVER_MATCH;

typedef struct DRIVER {
    const char *Name;
    DRIVER_CLASS Class;
    /* 匹配表（阶段 2 定型）；NULL = 旧行为，Probe 自扫 */
    const DRIVER_MATCH *Match;
    /*
     * Probe：有设备则返回 0 并可选写入 *OutPrivate；无设备返回非 0。
     * BusCtx 预留总线上下文：Match!=NULL 时框架传入 DEVICE_NODE *。
     */
    int (*Probe)(const struct DRIVER *Self, void *BusCtx, void **OutPrivate);
    int (*Bind)(DRIVER_INSTANCE *Inst);
    void (*Remove)(DRIVER_INSTANCE *Inst);
    /* 0 或 1：只要一个实例。大于 1 时同一张匹配表可以绑多台设备。 */
    int MaxInstances;
} DRIVER;

struct DRIVER_INSTANCE {
    const DRIVER *Driver;
    void *Private;
    int Bound;
};

int DriverRegister(const DRIVER *Driver);
/* 对已注册驱动调用 Probe；成功则 Bind 并记入实例表 */
int DriverProbeAll(void);
/* PR-D2：只 Probe 指定类；已达该驱动的实例上限则跳过 */
int DriverProbeClass(DRIVER_CLASS Class);
void DriverRemoveAll(void);

/* PR-DRV-match-logic：有表驱动按 DRIVER_MATCH 过滤设备；命中返回 1。 */
int DriverMatchesDevice(const DRIVER *Drv, const DEVICE_NODE *Dev);

UINTN DriverRegisteredCount(void);
const DRIVER *DriverRegisteredGet(UINTN Index);
UINTN DriverInstanceCount(void);
const DRIVER_INSTANCE *DriverInstanceGet(UINTN Index);

#endif
