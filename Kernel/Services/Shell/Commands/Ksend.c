/*
 * Ksend.c — 壳 `ksend [--no-reboot] [info|selftest|<目标路径>]`：串口增量写卡
 *
 * 实现全在板包里（Kernel/HAL/RiscV/Board/milk-v-duo-s/Ksend/）。这里只做分发，
 * 用弱引用（与 Wifi.c 的 wifiprobe 同法）：
 *   - 其它 RISC-V 靶（virt）没有这个模块 → 符号链成 NULL，只提示本板不支持；
 *   - x86_64 / arm64 连这段都不编。
 * 这样加一个命令不必为它动 HAL 头。
 *
 * v2：第二参数是卡上目标路径（PC 侧 --path 也走这条命令）；不给仍是 Kernel.elf。
 * v3：可选 --no-reboot（放在最前）→ 校验通过后留 shell，deploy.sh 用；
 *     目标可写 BOOT:boot.sd（板端 FileSystem 按卷选择后放行写启动分区）。
 * v4：可选 --no-backup → 跳过写前 .bak（boot.sd 这类可再生成的目标用）。
 */
#include "ShellPrivate.h"
#include "Console.h"

#if defined(__riscv)
extern int KsendInfo(void) __attribute__((weak));
extern int KsendSelftest(void) __attribute__((weak));
extern int KsendTransfer(const char *TargetPath, int Reboot, int Backup)
    __attribute__((weak));

/* 子命令大小写不敏感（壳本身对一级命令就不敏感），参数不便宜 */
static int KsendArgumentIs(const char *Argument, const char *Word) {
    if (Argument == 0) {
        return 0;
    }
    while (*Argument != 0 && *Word != 0) {
        char A = *Argument;
        char W = *Word;

        if (A >= 'A' && A <= 'Z') {
            A = (char)(A - 'A' + 'a');
        }
        if (W >= 'A' && W <= 'Z') {
            W = (char)(W - 'A' + 'a');
        }
        if (A != W) {
            return 0;
        }
        Argument++;
        Word++;
    }
    return *Argument == 0 && *Word == 0;
}
#endif

static void CommandKsend(int Argc, char **Argv) {
#if defined(__riscv)
    int Index = 1;
    int Reboot = 1;
    int Backup = 1;

    if (KsendInfo == 0 || KsendSelftest == 0 || KsendTransfer == 0) {
        gConsole.Write("ksend: not built for this board\n");
        return;
    }
    while (Index < Argc) {
        if (KsendArgumentIs(Argv[Index], "--no-reboot")) {
            Reboot = 0;
        } else if (KsendArgumentIs(Argv[Index], "--no-backup")) {
            Backup = 0;
        } else {
            break;
        }
        Index++;
    }
    if (Index < Argc) {
        if (KsendArgumentIs(Argv[Index], "info")) {
            (void)KsendInfo();
            return;
        }
        if (KsendArgumentIs(Argv[Index], "selftest")) {
            (void)KsendSelftest();
            return;
        }
        if (Index + 1 == Argc) {
            (void)KsendTransfer(Argv[Index], Reboot, Backup);
            return;
        }
        gConsole.Write("usage: ksend [--no-reboot] [--no-backup] "
                       "[info|selftest|<target path>]\n");
        return;
    }
    (void)KsendTransfer(0, Reboot, Backup);
#else
    (void)Argc;
    (void)Argv;
    gConsole.Write("ksend: Duo S only (riscv board build)\n");
#endif
}

void ShellCommandsKsendRegister(void) {
    gConsole.Register(
        "ksend",
        "ksend [--no-reboot] [--no-backup] [info|selftest|<target path>] "
        "— serial file (Duo S)",
        CommandKsend);
}
