/*
 * Commands.c — 内建命令注册汇总（命令体在同目录其它文件）
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"
#include "Desktop.h"

/*
 * ksend 的注册口在 Commands/Ksend.c。声明放这里而不是 ShellPrivate.h：
 * 板级开发工具只该动本目录，少碰一个公共头（其它靶不编命令体，
 * 弱引用保证 link 不炸）。
 */
void ShellCommandsKsendRegister(void);
/* GPIO 契约自检（板级实现在 HAL/<Arch>/Board/<board>/Gpio） */
void ShellCommandsGpioRegister(void);
/* I2C 扫描/读写（板级实现在 HAL/<Arch>/Board/<board>/I2c） */
void ShellCommandsI2cRegister(void);
/* Duo WiFi→lwIP 网桥探针（其它架构打印 only on milk-v-duo-s） */
void ShellCommandsWifiNetifRegister(void);

void ShellCommandsRegisterVirtMin(void) {
    ShellCommandsRegisterFs();
    ShellCommandsSystemRegisterVirtMin();
    /* Duo 等 ConsoleOnly：有网无桌面，须 net/setip/ping（否则 unknown: setip） */
    ShellCommandsNetRegister();
    /* Duo S：wifiprobe —— 真机上重跑 SD1(wifi-sd) SDIO 探测，不用重启 */
    ShellCommandsWifiProbeRegister();
    /* Duo S：ksend —— 串口增量写 Kernel.elf（开发期免拔卡） */
    ShellCommandsKsendRegister();
    ShellCommandsGpioRegister();
    ShellCommandsI2cRegister();
    ShellCommandsWifiNetifRegister();
}

void ShellCommandsRegister(void) {
    ShellCommandsRegisterFs();
    ShellCommandsSystemRegister();
    ShellCommandsThemeRegister();
    ShellCommandsUsbRegister();
    ShellCommandsBluetoothRegister();
    ShellCommandsWifiRegister();
    ShellCommandsNetRegister();
    ShellCommandsFsUiRegister();
    ShellCommandsFontProbeRegister();
    ShellCommandsGuiProbeRegister();
    DesktopIconsCommandRegister();
    ShellCommandsAudioRegister();
    ShellCommandsThreadRegister();
    ShellCommandsKsendRegister();
    ShellCommandsGpioRegister();
    ShellCommandsI2cRegister();
    ShellCommandsWifiNetifRegister();
}
