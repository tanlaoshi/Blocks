/*
 * Commands.c — 内建命令注册汇总（命令体在同目录其它文件）
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"

void ShellCommandsRegisterVirtMin(void) {
    ShellCommandsRegisterFs();
    ShellCommandsSystemRegisterVirtMin();
    /* Duo 等 ConsoleOnly：有网无桌面，须 net/setip/ping（否则 unknown: setip） */
    ShellCommandsNetRegister();
}

void ShellCommandsRegister(void) {
    ShellCommandsRegisterFs();
    ShellCommandsSystemRegister();
    ShellCommandsThemeRegister();
    ShellCommandsUsbRegister();
    ShellCommandsBluetoothRegister();
    ShellCommandsNetRegister();
    ShellCommandsFsUiRegister();
    ShellCommandsAudioRegister();
    ShellCommandsThreadRegister();
}
