/*
 * ConsoleInit.c — InitializeConsole（开机模块编织）
 *
 * 实现仍在 Services/Console（阶段 B 再并）。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Console.h"
#include "ShellCommands.h"
#include "Locale.h"

int InitializeConsole(void) {
    LocaleInitialize();
    ConsoleRegisterBuiltins();
    if (HalConsoleOnly()) {
        ShellCommandsRegisterVirtMin();
    } else {
        ShellCommandsRegister();
    }
    ConsoleUserAliasLoad();
    ConsoleInitialize();
    return 0;
}
