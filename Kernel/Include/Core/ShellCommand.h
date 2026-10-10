/*
 * ShellCommand.h — 串口 Shell 命令表（K22+）
 */
#ifndef SHELL_COMMAND_H
#define SHELL_COMMAND_H

typedef void (*SHELL_COMMAND_FN)(int Argc, char **Argv);

void ShellCommandInitialize(void);
void ShellCommandRunLine(const char *Line);
/* 往表注册一项；满或参数非法返回 -1 */
int ShellCommandRegister(const char *Name, const char *Help, SHELL_COMMAND_FN Fn);
/* K46：dbget/dbset（实现见 ShellCommandDataBase.c） */
void ShellCommandDataBaseRegister(void);
/* K47：store list/install（实现见 ShellCommandStore.c） */
void ShellCommandStoreRegister(void);

#endif
