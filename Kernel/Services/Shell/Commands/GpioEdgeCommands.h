/*
 * GpioEdgeCommands.h — gpio 边沿相关子命令（实现在 GpioEdgeCommands.c）
 *
 * 从 Gpio.c 拆出：守 300 行；边沿/轮询与基础读写分开。
 */
#ifndef SHELL_GPIO_EDGE_COMMANDS_H
#define SHELL_GPIO_EDGE_COMMANDS_H

void GpioCommandWatch(int Argc, char **Argv);
void GpioCommandEdge(int Argc, char **Argv);
void GpioCommandEdgeTake(int Argc, char **Argv);
void GpioCommandEdgeSelfTest(int Argc, char **Argv);

void GpioCommandIrqTest(int Argc, char **Argv);

#endif
