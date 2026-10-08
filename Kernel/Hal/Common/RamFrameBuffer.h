/*
 * RamFrameBuffer.h — QEMU virt 用「内存里的帧缓冲」
 *
 * 【初学者】
 * X64 亮屏常靠 UEFI GOP；Arm64/RiscV 的 QEMU virt 没有 GOP 时，
 * 可以在客人 RAM 里留一块像素区，再通过固件配置接口登记给虚拟机，
 * 让宿主窗口显示这块缓冲。本头声明该路径的入口。
 *
 * 实现放在 Hal/Common/（多架构共用），经 HalVideo 门面对外。
 */
#ifndef HAL_RAM_FRAME_BUFFER_H
#define HAL_RAM_FRAME_BUFFER_H

#include "BootInfo.h"

#define RAM_FRAME_BUFFER_WIDTH  1920u
#define RAM_FRAME_BUFFER_HEIGHT 1080u

/*
 * 在 *FreeStart 处切出帧缓冲，登记给虚拟机显示，填 Info 的帧缓冲字段，
 * 并推进 *FreeStart。成功 0；无配置接口或内存不足则 -1（帧缓冲保持 0）。
 */
int RamFrameBufferSetup(BOOT_INFO *Info, UINT64 FirmwareConfigBase,
                        UINT64 *FreeStart, UINT64 RamEnd);

#endif
