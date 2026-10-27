/*
 * HalBluetooth.h — 蓝牙门面（bt-facade / bt-3arch）
 *
 * 三架构同形；板差异只在 HAL/<Arch>/Hal/HalBluetooth.c（及后续 Board/USB）。
 * 课轨：NUC 板载蓝牙（常见内部 USB HCI）；真探针在 btx-1…。
 * 无硬件或未探针成功 → Ready=0；禁止空实现谎称已配对。
 */
#ifndef HAL_BLUETOOTH_H
#define HAL_BLUETOOTH_H

#include "BootTypes.h"

#define HAL_BLUETOOTH_ADDRESS_LENGTH 6u
#define HAL_BLUETOOTH_NAME_MAX 64u
#define HAL_BLUETOOTH_SCAN_MAX 16u

typedef struct {
    UINT8 Address[HAL_BLUETOOTH_ADDRESS_LENGTH]; /* HCI 线序 */
    UINT8 ClassOfDevice[3];
    char Name[HAL_BLUETOOTH_NAME_MAX];
} HAL_BLUETOOTH_SCAN_ENTRY;

/* 0=门面已挂（可无控制器）；不表示 Ready */
int HalBluetoothInitialize(void);
/* 1=HCI 可用；无硬件 / 未枚举必须 0 */
int HalBluetoothReady(void);

/*
 * 读设备地址（6 字节，HCI 线序：LSB first；规格名 BD_ADDR）。
 * 未 Ready → -1；成功 → 0。
 */
int HalBluetoothGetDeviceAddress(UINT8 Out[HAL_BLUETOOTH_ADDRESS_LENGTH]);

/*
 * 本地名 → Out（NUL 结尾，最多 NameMax-1）。
 * 未 Ready 或无名 → -1；成功 → 0。
 */
int HalBluetoothGetLocalName(char *Out, UINTN NameMax);

/*
 * 经典 Inquiry；TimeoutMs=0 → 默认约 8s。鼠优先分片泵。
 * 成功 → 条数（0…OutMax）；未 Ready / 无硬件 → -1。
 */
int HalBluetoothScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                     UINT32 TimeoutMs);

#endif
