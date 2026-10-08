/*
 * Kernel.h — Common 入口汇总头（薄）
 *
 * KernelMain：三架构合流后的大门（Handoff 调用；声明放这里，不经 BootInfo 仓）。
 */
#ifndef KERNEL_H
#define KERNEL_H

#include "BootInfoTypes.h"

void KernelMain(const BOOT_INFO *Info);

#endif
