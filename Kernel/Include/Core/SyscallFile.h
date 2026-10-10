/*
 * SyscallFile.h — K51：用户态文件 fd（根目录 8.3）
 */
#ifndef SYSCALL_FILE_H
#define SYSCALL_FILE_H

#include "BootTypes.h"

#define SYS_OPEN 9u

#define FD_FILE_BASE 16
#define FD_FILE_COUNT 4

/* flags：0=只读；1=写/建 */
int SyscallFileOpen(const char *Path, UINT64 Flags);
int SyscallFileRead(int Fd, void *Buf, UINTN Len);
int SyscallFileWrite(int Fd, const void *Buf, UINTN Len);
int SyscallFileClose(int Fd);
int SyscallFileIsFd(UINT64 Fd);

#endif
