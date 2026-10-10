/*
 * Device.c — PR-K5：设备框架空壳
 *
 * 【初学者】
 * - Core/Driver：将来挂接 DEVICE 链表；当前 Initialize/Enumerate 为空。
 * - 谁调用：DriverInitialize。
 * - 边界：不 include Hal 头；枚举逻辑后续专刀。
 */
#include "Device.h"

/*
 * DeviceInitialize — 设备链表/表占位
 *
 * 做什么：当前无操作；预留 DRIVER 注册点。
 * 谁调用：DriverInitialize。
 */
void DeviceInitialize(void) {
}

/*
 * DeviceEnumerateAll — 扫描并注册设备（占位）
 *
 * 做什么：当前无操作；将来 PCI/USB 枚举在此或 Hal 回调。
 * 谁调用：DriverInitialize。
 */
void DeviceEnumerateAll(void) {
}
