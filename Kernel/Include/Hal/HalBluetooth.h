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

/*
 * 起机 / 壳 bt init。
 * 返回 1=Ready；0=未 Ready（无 HCI / Reset / BD_ADDR 失败）。
 * 已 Ready → 1；未首探 → 探一次；已探失败 → 再探一次。
 */
int HalBluetoothInitialize(void);
/* 1=HCI 可用；无硬件 / 未枚举必须 0 */
int HalBluetoothReady(void);
/* 等 Worker 首探结束或超时；返回 Ready（1/0） */
int HalBluetoothWaitReady(UINT32 TimeoutMilliseconds);
/*
 * 壳诊断：ProbeDone / UsbSeen / HciResetOk / VendorId / ProductId / RootPort。
 * 指针可空。
 */
void HalBluetoothBringUpStatus(int *ProbeDoneOut, int *UsbSeenOut,
                               int *HciResetOkOut, UINT16 *VendorIdOut,
                               UINT16 *ProductIdOut, UINT32 *RootPortOut);

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

/* 1=Inquiry+Page Scan 开，可被附近搜到；未 Ready → 0 */
int HalBluetoothDiscoverable(void);
/* On≠0 开可见（名 Blocks）；0 关闭。成功 → 0；失败 → -1 */
int HalBluetoothSetDiscoverable(int On);

/* PR-BTM-1：HCI 常驻泵切片；未 Ready / 非 X64 USB HCI → 空操作 */
void HalBluetoothHostPump(void);

/* PR-BTM-2：ACL 流控查询；未 Ready → -1 */
int HalBluetoothAclCreditQuery(UINT16 *PacketLengthOut, UINT16 *PacketsFreeOut);
/* 发一帧测试 ACL OUT（无连接句柄亦可）；成功 → 0 */
int HalBluetoothAclOutTest(void);

/*
 * PR-BTM-3：LE 扫描；TimeoutMs=0 → 默认约 8s。鼠优先分片。
 * 成功 → 条数；未 Ready / 无 LE → -1。
 */
int HalBluetoothLeScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                       UINT32 TimeoutMs);

/*
 * PR-BTM-4：按最近一次 lescan 的 1-based 序号连；TimeoutMs=0 → 约 15s。
 * 成功 → 0 且串口 `bt: le connected handle=`；失败 → -1。
 */
int HalBluetoothLeConnect(UINTN OneBasedIndex, UINT32 TimeoutMs);
/* 断当前 LE；无连接 → 0 */
int HalBluetoothLeDisconnect(void);
/* 最近一次 LE 命令失败：Opcode / HCI Status（0xFE=等 CC 失败） */
void HalBluetoothLeLastError(UINT16 *OpcodeOut, UINT8 *StatusOut);

/*
 * PR-BTM-5：SMP Just Works（须已 LE 连接）；TimeoutMs=0 → 约 20s。
 * 成功 → 0（串口 `bt: smp ok`）；失败 → -1。
 */
int HalBluetoothSmpJustWorks(UINT32 TimeoutMs);

#endif
