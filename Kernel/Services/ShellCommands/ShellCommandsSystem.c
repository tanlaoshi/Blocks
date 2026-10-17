/*
 * ShellCommandsSystem.c — PR-S3-shellsys-1：info/mem/reboot/halt/lsdev + 注册汇总
 *
 * exec/ps/kill/set priority 见 ShellCommandsSystemProc.c。
 */
#include "ShellPrivate.h"
#include "BootInfo.h"
#include "Console.h"
#include "Device.h"
#include "Driver.h"
#include "PhysicalMemory.h"
#include "Hal.h"

static void CommandInfo(int Argc, char **Argv) {
    const BOOT_INFO *Info = BootInfoGet();
    (void)Argc;
    (void)Argv;
    if (!Info) {
        gConsole.Write("video (no boot info)\n");
        return;
    }
    gConsole.Write("video ");
    gConsole.WriteHex32(Info->HorizontalResolution);
    gConsole.Write(" x ");
    gConsole.WriteHex32(Info->VerticalResolution);
    gConsole.Write(" fb=");
    gConsole.WriteHex64(Info->FrameBufferBase);
    gConsole.Write("\n");
}

static void CommandMemory(int Argc, char **Argv) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT64 RegionBytes = 0;
    UINT32 i;

    (void)Argc;
    (void)Argv;
    gConsole.Write("physical memory\n  free  ");
    gConsole.WriteHex64(PhysicalMemoryFreePageCount() << PAGE_SHIFT);
    gConsole.Write(" bytes (");
    gConsole.WriteHex32((UINT32)PhysicalMemoryFreePageCount());
    gConsole.Write(" pages)\n  total ");
    gConsole.WriteHex64(PhysicalMemoryTotalPages() << PAGE_SHIFT);
    gConsole.Write(" bytes tracked\n");
    if (Info) {
        for (i = 0; i < Info->RegionCount; i++) {
            RegionBytes += Info->Regions[i].Size;
        }
        gConsole.Write("  boot  ");
        gConsole.WriteHex64(RegionBytes);
        gConsole.Write(" bytes in ");
        gConsole.WriteHex32(Info->RegionCount);
        gConsole.Write(" region(s)\n");
    }
}

static void CommandMemtest(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    UINT64 Before = PhysicalMemoryFreePageCount();
    void *Page = PhysicalMemoryAllocatePage();
    if (Page == 0) {
        gConsole.Write("memtest: alloc failed\n");
        return;
    }
    UINT8 *Bytes = (UINT8 *)Page;
    for (int i = 0; i < (int)PAGE_SIZE; i++) {
        Bytes[i] = (UINT8)i;
    }
    for (int i = 0; i < (int)PAGE_SIZE; i++) {
        if (Bytes[i] != (UINT8)i) {
            gConsole.Write("memtest: verify failed at ");
            gConsole.WriteHex32((UINT32)i);
            gConsole.Write("\n");
            PhysicalMemoryFreePage(Page);
            return;
        }
    }
    gConsole.Write("memtest: page ");
    gConsole.WriteHex64((UINT64)(UINTN)Page);
    gConsole.Write(" ok, freeing\n");
    PhysicalMemoryFreePage(Page);
    gConsole.Write("memtest: free pages ");
    gConsole.WriteHex32((UINT32)Before);
    gConsole.Write(" -> ");
    gConsole.WriteHex32((UINT32)PhysicalMemoryFreePageCount());
    gConsole.Write("\n");
}

static void CommandReboot(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    gConsole.Write("rebooting...\n");
    HalConsoleWriteSerial("shell: reboot\n");
    HalCpuReboot();
}

static void CommandHalt(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    gConsole.Write("halt\n");
    HalCpuPark();
}

/* PR-D4：列出已绑定驱动（TOY_DRIVER.Name + 类） */
static const char *DriverClassName(TOY_DRIVER_CLASS Class) {
    switch (Class) {
    case TOY_DRIVER_CLASS_BLOCK:
        return "block";
    case TOY_DRIVER_CLASS_INPUT:
        return "input";
    case TOY_DRIVER_CLASS_NET:
        return "net";
    case TOY_DRIVER_CLASS_DISPLAY:
        return "display";
    case TOY_DRIVER_CLASS_AUDIO:
        return "audio";
    default:
        return "?";
    }
}

static void CommandLsdev(int Argc, char **Argv) {
    UINTN i;
    UINTN Bound = 0;

    if (DeviceListDumpArgs(Argc, Argv, gConsole.Write) != 0) {
        return;
    }

    for (i = 0; i < ToyDriverInstanceCount(); i++) {
        const TOY_DRIVER_INSTANCE *Inst = ToyDriverInstanceGet(i);
        if (!Inst || !Inst->Bound || !Inst->Driver || !Inst->Driver->Name) {
            continue;
        }
        Bound++;
    }
    gConsole.Write("\n=== Bound Drivers (");
    gConsole.WriteHex32((UINT32)Bound);
    gConsole.Write(") ===\n");
    if (Bound == 0) {
        gConsole.Write("  (none)\n");
        return;
    }
    for (i = 0; i < ToyDriverInstanceCount(); i++) {
        const TOY_DRIVER_INSTANCE *Inst = ToyDriverInstanceGet(i);
        if (!Inst || !Inst->Bound || !Inst->Driver || !Inst->Driver->Name) {
            continue;
        }
        gConsole.Write("  ");
        gConsole.Write(Inst->Driver->Name);
        gConsole.Write("  ");
        gConsole.Write(DriverClassName(Inst->Driver->Class));
        /* PR-H4e-2：net + e1000* 时附链路 */
        if (Inst->Driver->Class == TOY_DRIVER_CLASS_NET) {
            int Up = 0;
            UINT32 Mbps = 0;
            int Fd = 0;

            if (HalNetGetLinkInfo(&Up, &Mbps, &Fd)) {
                gConsole.Write("  link=");
                gConsole.Write(Up ? "up" : "down");
                if (Up) {
                    if (Mbps == 1000u) {
                        gConsole.Write(" 1000");
                    } else if (Mbps == 100u) {
                        gConsole.Write(" 100");
                    } else if (Mbps == 10u) {
                        gConsole.Write(" 10");
                    }
                    gConsole.Write(Fd ? "/FD" : "/HD");
                }
            }
        }
        gConsole.Write("\n");
    }
}

/* PR-K-uart-shell：ready 后 ToyLog 不进 COM1；log / log save 拉 RUNTIME.LOG */
static void CommandLog(int Argc, char **Argv) {
    if (Argc >= 2 && Argv[1][0] == 's' && Argv[1][1] == 'a' && Argv[1][2] == 'v' &&
        Argv[1][3] == 'e' && Argv[1][4] == 0) {
        const char *Path = (Argc >= 3) ? Argv[2] : "RUNTIME.LOG";
        if (gConsole.RuntimeLogSave(Path) != 0) {
            gConsole.Write("log: save failed\n");
            return;
        }
        gConsole.Write("log: saved ");
        gConsole.Write(Path);
        gConsole.Write("\n");
        return;
    }
    if (Argc >= 2) {
        gConsole.Write("usage: log | log save [path]\n");
        return;
    }
    (void)gConsole.RuntimeLogFlush();
    gConsole.RuntimeLogShow();
}

void ShellCommandsSystemRegisterVirtMin(void) {
    ShellCommandsSystemProcRegisterVirtMin();
    gConsole.Register2("show", "memory", "physical memory stats", CommandMemory);
    gConsole.RegisterAliasLine("mem", "show", "memory");
    gConsole.Register2("list", "devices", "list devices + bound drivers", CommandLsdev);
    gConsole.RegisterAliasLine("lsdev", "list", "devices");
    gConsole.Register("log", "runtime log; log save [RUNTIME.LOG]", CommandLog);
    gConsole.Register("halt", "stop CPU", CommandHalt);
    gConsole.RegisterAlias("halt", "exit");
    gConsole.RegisterAlias("halt", "quit");
}

void ShellCommandsSystemRegister(void) {
    ShellCommandsSystemProcRegister();

    gConsole.Register2("list", "devices", "list devices + bound drivers", CommandLsdev);
    gConsole.RegisterAliasLine("lsdev", "list", "devices");

    gConsole.Register2("show", "memory", "physical memory stats", CommandMemory);
    gConsole.Register2("show", "info", "boot framebuffer info", CommandInfo);
    gConsole.RegisterAliasLine("mem", "show", "memory");
    gConsole.RegisterAliasLine("memory", "show", "memory");
    gConsole.RegisterAliasLine("info", "show", "info");

    gConsole.Register2("test", "memory", "alloc/verify/free one page", CommandMemtest);
    gConsole.RegisterAliasLine("memtest", "test", "memory");

    gConsole.Register("log", "runtime log; log save [RUNTIME.LOG]", CommandLog);
    gConsole.Register("reboot", "reset CPU (QEMU display: quit+./run-split.sh)", CommandReboot);
    gConsole.Register("halt", "stop CPU", CommandHalt);
    gConsole.RegisterAlias("halt", "exit");
    gConsole.RegisterAlias("halt", "quit");
}
