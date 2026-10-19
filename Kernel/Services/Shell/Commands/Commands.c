/*
 * Commands.c — 内建命令注册汇总（命令体在同目录其它文件）
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
