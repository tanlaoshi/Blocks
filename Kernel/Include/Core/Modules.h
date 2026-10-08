/*
 * Modules.h — 开机模块表入口
 *
 * 【初学者】
 * 可以有多张表（串口子集 / virt 桌面 / 全量），由 KernelMain 按能力旗标挑选。
 * 当前 Full：… → USB → FileSystem → Network → Scheduler → Console。
 */
#ifndef MODULES_H
#define MODULES_H

int ModulesRunFull(void);

#endif
