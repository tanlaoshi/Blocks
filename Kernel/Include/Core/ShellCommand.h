/*
 * ShellCommand.h — 串口 Shell 命令表（K22+）
 *
 * 实现在 Core/Console/ShellCommand/（目录即命名空间；子文件不叠 ShellCommand）。
 */
#ifndef SHELL_COMMAND_H
#define SHELL_COMMAND_H

typedef void (*SHELL_COMMAND_FN)(int Argc, char **Argv);

void ShellCommandInitialize(void);
void ShellCommandRunLine(const char *Line);
/* 往表注册一项；满或参数非法返回 -1 */
int ShellCommandRegister(const char *Name, const char *Help, SHELL_COMMAND_FN Fn);

/* 子文件注册（夹内符号不叠目录名） */
void FileSystemRegister(void);
void ThemeRegister(void);
void NetworkRegister(void);
void NetworkTcpRegister(void);
void NetworkUdpRegister(void);
void DataBaseRegister(void);
void StoreRegister(void);

#endif
