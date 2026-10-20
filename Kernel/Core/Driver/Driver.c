/*
 * Driver.c — 驱动注册表（开机入口 InitializeDriver，以及 Probe/Bind/Remove）
 *
 * 开机：枚举设备、注册驱动，并只 Probe Block（勿 ProbeAll）。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Driver.h"
#include "Device.h"
#include "SerialLog.h"
#include "Debug.h"

static const TOY_DRIVER *gDrivers[TOY_DRIVER_MAX_DRIVERS];
static UINTN gDriverCount;

static TOY_DRIVER_INSTANCE gInstances[TOY_DRIVER_MAX_INSTANCES];
static UINTN gInstanceCount;

int ToyDriverRegister(const TOY_DRIVER *Driver) {
    if (!Driver || !Driver->Name || !Driver->Probe) {
        return -1;
    }
    if (gDriverCount >= TOY_DRIVER_MAX_DRIVERS) {
        LogDrv("driver: register full\n");
        return -1;
    }
    gDrivers[gDriverCount++] = Driver;
    return 0;
}

int ToyDriverProbeAll(void) {
    return ToyDriverProbeClass(TOY_DRIVER_CLASS_NONE);
}

static int InstanceLimit(const TOY_DRIVER *D) {
    if (D->MaxInstances > 1) {
        return D->MaxInstances;
    }
    return 1;
}

static int BoundCount(const TOY_DRIVER *D) {
    UINTN i;
    int Count = 0;

    for (i = 0; i < gInstanceCount; i++) {
        if (gInstances[i].Driver == D && gInstances[i].Bound) {
            Count++;
        }
    }
    return Count;
}

/*
 * Class == TOY_DRIVER_CLASS_NONE：Probe 全部（D1 行为）。
 * 其它：只 Probe 该类；已绑定的驱动跳过（供 HalBlockInitialize 在 VMM 后再试 virtio-blk）。
 *
 * PR-DRV-match-logic：
 *   Match==NULL → 旧路径 Probe(Self, 0, …)（驱动自扫 PCI/DTB）。
 *   Match!=NULL → 遍历未绑定设备，DriverMatchesDevice 命中则
 *                 Probe(Self, (void *)Dev, …)，成功必须 DeviceBindDriver。
 *                 MaxInstances 为 0 或 1 时命中即停。大于 1 时继续下一台，直到够数。
 */
static TOY_DRIVER_INSTANCE *DriverAdoptInstance(const TOY_DRIVER *D, void *Private) {
    TOY_DRIVER_INSTANCE *Inst;

    if (gInstanceCount >= TOY_DRIVER_MAX_INSTANCES) {
        LogDrv("driver: instance full\n");
        if (D->Remove) {
            TOY_DRIVER_INSTANCE Tmp;
            Tmp.Driver = D;
            Tmp.Private = Private;
            Tmp.Bound = 0;
            D->Remove(&Tmp);
        }
        return 0;
    }
    Inst = &gInstances[gInstanceCount];
    Inst->Driver = D;
    Inst->Private = Private;
    Inst->Bound = 0;
    if (D->Bind && D->Bind(Inst) != 0) {
        if (D->Remove) {
            D->Remove(Inst);
        }
        Inst->Driver = 0;
        Inst->Private = 0;
        return 0;
    }
    Inst->Bound = 1;
    gInstanceCount++;
    return Inst;
}

int ToyDriverProbeClass(TOY_DRIVER_CLASS Class) {
    UINTN i;
    int Bound = 0;

    for (i = 0; i < gDriverCount; i++) {
        const TOY_DRIVER *D = gDrivers[i];
        void *Private = 0;
        TOY_DRIVER_INSTANCE *Inst;

        if (Class != TOY_DRIVER_CLASS_NONE && D->Class != Class) {
            continue;
        }
        if (BoundCount(D) >= InstanceLimit(D)) {
            continue;
        }
        if (D->Match == NULL) {
            if (D->Probe(D, 0, &Private) != 0) {
                continue;
            }
            if (DriverAdoptInstance(D, Private)) {
                Bound++;
            }
        } else {
            int Idx;
            for (Idx = 0; Idx < DeviceCount(); Idx++) {
                DEVICE_NODE *Dev = DeviceGet(Idx);
                if (!Dev || Dev->Bound) {
                    continue;
                }
                if (!DriverMatchesDevice(D, Dev)) {
                    continue;
                }
                if (D->Probe(D, (void *)Dev, &Private) != 0) {
                    continue;
                }
                Inst = DriverAdoptInstance(D, Private);
                if (Inst) {
                    DeviceBindDriver(Dev, D, Inst);
                    Bound++;
                }
                if (InstanceLimit(D) == 1 || BoundCount(D) >= InstanceLimit(D)) {
                    break;
                }
            }
        }
    }
#if KERNEL_DEBUG
    LogDrv("driver: registered=");
    LogDrvHex32((UINT32)gDriverCount);
    LogDrv(" bound=");
    LogDrvHex32((UINT32)gInstanceCount);
    LogDrv(" (+");
    LogDrvHex32((UINT32)Bound);
    LogDrv(")\n");
#else
    (void)Bound;
#endif
    return 0;
}

void ToyDriverRemoveAll(void) {
    UINTN i;

    for (i = gInstanceCount; i > 0; i--) {
        TOY_DRIVER_INSTANCE *Inst = &gInstances[i - 1];
        if (Inst->Driver && Inst->Driver->Remove) {
            Inst->Driver->Remove(Inst);
        }
        Inst->Driver = 0;
        Inst->Private = 0;
        Inst->Bound = 0;
    }
    gInstanceCount = 0;
}

UINTN ToyDriverRegisteredCount(void) {
    return gDriverCount;
}

const TOY_DRIVER *ToyDriverRegisteredGet(UINTN Index) {
    if (Index >= gDriverCount) {
        return 0;
    }
    return gDrivers[Index];
}

UINTN ToyDriverInstanceCount(void) {
    return gInstanceCount;
}

const TOY_DRIVER_INSTANCE *ToyDriverInstanceGet(UINTN Index) {
    if (Index >= gInstanceCount) {
        return 0;
    }
    return &gInstances[Index];
}

int InitializeDriver(void) {
    /*
     * 只 Probe Block：ATA PIO / AHCI / NVMe 在 VMM 后可绑 MMIO。
     * 勿 ProbeAll：ps2-kbd 会跑 Ps2InitHw；真机无 8042 时 STATUS 浮空 → 死循环。
     * Input / Net 仍由后续 usb / network 模块 Probe。
     */
    DeviceInitialize();
    DeviceEnumerateAll();
    LogBoot("Boot: Device Enumerate Done\n");
    HalDriverRegister();
    LogBoot("Boot: Driver Register OK\n");
    (void)DisplayHardwareInitialize();
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_BLOCK);
    LogBoot("Boot: Driver Block Probe Done\n");
    return 0;
}
