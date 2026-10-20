/*
 * KernelModulesPrivate.h — KernelModules / 开机模块入口
 */
#ifndef KERNEL_MODULES_PRIVATE_H
#define KERNEL_MODULES_PRIVATE_H

int SerialModuleInitialize(void);
int PhysicalMemoryInitialize(void);
int VirtualMemoryInitialize(void);
int VideoInitialize(void);
int DisplayHardwareInitialize(void);
int CpuInitialize(void);
int UsbInitialize(void);
int UsbUartInitialize(void);
int FileSystemInitialize(void);
int GuiInitialize(void);
int NetworkInitialize(void);
int DriverInitialize(void);
int SchedulerInitialize(void);
int ConsoleInitialize(void);

#endif
