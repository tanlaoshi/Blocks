/*
 * ShellCmd.h — 串口 Shell 命令表（K22+）
 */
#ifndef SHELL_CMD_H
#define SHELL_CMD_H

typedef void (*SHELL_CMD_FN)(int Argc, char **Argv);

void ShellCmdInitialize(void);
void ShellCmdRunLine(const char *Line);
/* 往表注册一项；满或参数非法返回 -1 */
int ShellCmdRegister(const char *Name, const char *Help, SHELL_CMD_FN Fn);

#endif
