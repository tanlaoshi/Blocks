/*
 * ShellCommands.c — PR-S-shell-split-3：注册汇总（命令体见 ShellCommands*.c）
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"

void ShellCommandsRegisterVirtMin(void) {
    ShellCommandsRegisterFs();
    ShellCommandsSystemRegisterVirtMin();
}

void ShellCommandsRegister(void) {
    ShellCommandsRegisterFs();
    ShellCommandsSystemRegister();
    ShellCommandsThemeRegister();
    ShellCommandsUsbRegister();
    ShellCommandsNetRegister();
    ShellCommandsFsUiRegister();
    ShellCommandsAudioRegister();
    ShellCommandsThreadRegister();
}
