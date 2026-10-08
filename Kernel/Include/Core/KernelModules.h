/*
 * KernelModules.h — 开机模块表入口
 *
 * 【初学者】
 * 可以有多张表（串口子集 / virt 桌面 / 全量），由 KernelMain 按能力旗标挑选。
 * 当前 Full：Serial → Memory → Driver → VirtualMemory → Video。
 */
#ifndef KERNEL_MODULES_H
#define KERNEL_MODULES_H

int KernelModulesRunFull(void);

#endif
