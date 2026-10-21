/*
 * LogFile.h — 日志落盘（Console/LogFile.c）
 *
 * 开机缓冲 → 各 BLOCKS 卷：BOOT-BOOTDISK.LOG / BOOT-OTHER.LOG。
 * 运行时环 → RUNTIME-BOOTDISK.LOG / RUNTIME-OTHER.LOG。
 * BOOTDISK = 本卷所在盘即加载 EFI 的盘。总开关 FILE_LOG 见 SerialConfig.h。
 */
#ifndef LOG_FILE_H
#define LOG_FILE_H

/* 挂卷后、桌面起来后再各写一次。后一次覆盖前一次。 */
void BootLogFlush(void);

#endif
