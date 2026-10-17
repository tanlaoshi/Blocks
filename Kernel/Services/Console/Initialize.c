/*
 * Initialize.c — InitializeConsole（开机模块表；Services/Console 本层）
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Console.h"
#include "ConsolePrivate.h"
#include "ShellCommands.h"
#include "Store.h"
#include "Locale.h"

void ConsolePublishAll(void) {
    ConsolePublishWrite(&gConsole);
    ConsolePublishInput(&gConsole);
    ConsolePublishHistory(&gConsole);
    ConsolePublishSerial(&gConsole);
    ConsolePublishCommand(&gConsole);
    ConsolePublishBuiltin(&gConsole);
    ConsolePublishAlias(&gConsole);
    ConsolePublishFocus(&gConsole);
    ConsolePublishShell(&gConsole);
    ConsolePublishScroll(&gConsole);
    ConsolePublishScrollbar(&gConsole);
    ConsolePublishScrollback(&gConsole);
    ConsolePublishLog(&gConsole);
}

int InitializeConsole(void) {
    ConsolePublishAll();
    StoreRegisterPathGuard();
    LocaleInitialize();
    gConsole.RegisterBuiltins();
    if (HalConsoleOnly()) {
        ShellCommandsRegisterVirtMin();
    } else {
        ShellCommandsRegister();
    }
    gConsole.UserAliasLoad();
    gConsole.Initialize();
    return 0;
}
