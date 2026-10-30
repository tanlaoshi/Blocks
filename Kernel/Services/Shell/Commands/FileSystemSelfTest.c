/*
 * FileSystemSelfTest.c — Shell `fs selftest`：跨平台文件系统写自检
 *
 * 判据与打印都在 Services/FileSystem/SelfTest.c（DUO 的 `ksend selftest`
 * 复用同一实现）。本文件只有命令分发与注册；注册入口由
 * ShellCommandsFsExtraRegister 调用，不再占 Commands.c 的共享注册口。
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"
#include "FileSystem.h"
#include "Console.h"

static void CommandFsSelfTest(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    (void)FileSystemWriteSelfTest();
}

void ShellCommandsFsSelfTestRegister(void) {
    gConsole.Register2("fs", "selftest",
                       "8 KB write + readback + overwrite on BLOCKS:FSELFT.BIN",
                       CommandFsSelfTest);
    gConsole.RegisterAliasLine("fsselftest", "fs", "selftest");
}
