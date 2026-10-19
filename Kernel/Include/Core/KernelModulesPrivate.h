/*
 * KernelModulesPrivate.h — KernelModules / Init 内部
 */
#ifndef KERNEL_MODULES_PRIVATE_H
#define KERNEL_MODULES_PRIVATE_H

int SerialModuleInitialize(void);
int InitializePhysicalMemory(void);
int InitializeVirtualMemory(void);
int VideoInitialize(void);
int DisplayHardwareInitialize(void);
int CpuInitialize(void);
int UsbInitialize(void);
int UsbUartInitialize(void);
int InitializeFileSystem(void);
int InitializeGui(void);
int InitializeNetwork(void);
int InitializeDriver(void);
int InitializeScheduler(void);
int InitializeConsole(void);

#endif
