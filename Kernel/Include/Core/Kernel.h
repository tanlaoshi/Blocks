/*
 * Kernel.h — BringUp 入口（架构无关；实现在 BringUp/Kernel.c）
 */
#ifndef KERNEL_H
#define KERNEL_H

/* BringUp/<Arch>/KernelHandoff 填好 BOOT_INFO 后调用 */
void KernelMain(void);

#endif
