/*
 * Console.h — 串口控制台壳（K8）
 *
 * Initialize：横幅；Run：提示符 + 读行回显（无完整命令表）。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

int ConsoleInitialize(void);
/* 不返回：串口交互循环 */
void ConsoleRun(void);

#endif
