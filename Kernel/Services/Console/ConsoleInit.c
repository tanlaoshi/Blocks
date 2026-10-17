/*
 * ConsoleInit.c — InitializeConsole（开机模块表；Services/Console 本层）
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Console.h"
#include "ShellCommands.h"
#include "Store.h"
#include "Locale.h"

int InitializeConsole(void) {
    StoreRegisterPathGuard();
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
