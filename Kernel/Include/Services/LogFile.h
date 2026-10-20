/*
 * LogFile.h — 日志落盘（Console/LogFile.c）
 *
 * 开机缓冲写 BOOT.LOG。总开关 FILE_LOG，见 SerialConfig.h。
 * 关着时 BootLogFlush 是空函数，不写盘。
 */
#ifndef LOG_FILE_H
#define LOG_FILE_H

/* 挂卷后、桌面起来后再各写一次。后一次覆盖前一次。 */
void BootLogFlush(void);

#endif
