/*
 * Console.h — 串口控制台壳（K8）
 *
 * Initialize：横幅 + ShellCommand；Run：提示符 + 命令表分发。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

int ConsoleInitialize(void);
void ConsoleRefreshBanner(void);
/* 只画到背缓冲（拖窗用，Present 由 Gui 统一做） */
void ConsolePaintBannerBack(void);
/* 不返回：串口交互循环 */
void ConsoleRun(void);

#endif
