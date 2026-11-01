/*
 * GpioSyntax.h — gpio 命令的解析接口（实现在 GpioSyntax.c）
 */
#ifndef SHELL_GPIO_SYNTAX_H
#define SHELL_GPIO_SYNTAX_H

#include "BootTypes.h"

#define GPIO_ARG_COMMAND  1
#define GPIO_ARG_FIRST    2
#define GPIO_ARG_SECOND   3

void GpioWriteUsage(void);
const char *GpioArg(int Argc, char **Argv, int Index);
int GpioParseNumber(const char *Text, UINT32 *Out);
int GpioParsePinText(const char *Text, UINT32 *OutPin);
int GpioParsePin(int Argc, char **Argv, UINT32 *OutPin);
int GpioTextEqual(const char *Left, const char *Right);

#endif
