/*
 * ShellCmd.h — 串口 Shell 命令表（K22）
 *
 * 【初学者】
 * 对标现网 ConsoleRegisterBuiltins 入口：名 → 帮助串 → 处理函数。
 * 本刀只有 help/clear/echo/hello；后刀往表里加 ls/ping…
 */
#ifndef SHELL_CMD_H
#define SHELL_CMD_H

void ShellCmdInitialize(void);
/* 跑一行；空行直接返回；未知命令打提示 */
void ShellCmdRunLine(const char *Line);

#endif
