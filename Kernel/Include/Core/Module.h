/*
 * Module.h — 开机「积木表」里的一格
 *
 * 【初学者】
 * 内核不是 main() 里写一千行 if，而是一张表：
 *   { "Memory", InitializePhysicalMemory },
 *   { "Scheduler", InitializeScheduler },
 *   …
 * ModulesRun() 按顺序调用每个 Init（实现在 Modules.c）。失败就停。
 *
 * 想加子系统？写 Init 函数，往表里插一行即可（顺序即依赖顺序）。
 *
 * 【积木】表本身是胶水；表里每一项对应的「政策实现」才可能是可替换积木。
 */
#ifndef MODULE_H
#define MODULE_H

typedef struct {
    const char *Name;  /* 串口日志里看到的名字，如 Serial */
    int (*Init)(void); /* 0=成功，非 0=失败 */
} MODULE;

int ModulesRun(const MODULE *List, int Count);

#endif
