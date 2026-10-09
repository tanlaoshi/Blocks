/*
 * Process.h — 用户 ELF 装载运行（K19 HELLO；K25 exec）
 */
#ifndef PROCESS_H
#define PROCESS_H

/* 读 RootFs HELLO.ELF、装载、跑到 exit；成功 0 */
int ProcessRunHello(void);

/* 按根目录路径 exec（如 "HELLO.ELF"）；成功 0 */
int ProcessExecPath(const char *Path);

/* ps 用：上次 exec 名（无则空串） */
const char *ProcessLastExecName(void);

#endif
