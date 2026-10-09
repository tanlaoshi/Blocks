#ifndef PROCESS_H
#define PROCESS_H

/* 读 RootFs HELLO.ELF、装载、跑到 exit；成功 0 */
int ProcessRunHello(void);
/* 按根路径装载 ELF（如 "HELLO.ELF"）；成功 0 */
int ProcessExecPath(const char *Path);
/* 最近一次 exec 的文件名；无则 "" */
const char *ProcessLastExecName(void);

#endif
