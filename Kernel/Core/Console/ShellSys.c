/*
 * ShellSys.c — K25：mem / ps / exec
 */
#include "ShellCmd.h"
#include "HalSerial.h"
#include "PhysicalMemory.h"
#include "Process.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static void PutU64(UINT64 V) {
    char B[24];
    int n = 0;
    int i;
    UINT64 X = V;
    if (X == 0) {
        Put("0");
        return;
    }
    while (X && n < 20) {
        B[n++] = (char)('0' + (X % 10u));
        X /= 10u;
    }
    for (i = n - 1; i >= 0; i--) {
        char One[2];
        One[0] = B[i];
        One[1] = 0;
        Put(One);
    }
}

static void CmdMem(int Argc, char **Argv) {
    UINT64 Free = PhysicalMemoryFreePageCount();
    UINT64 Total = PhysicalMemoryTotalPages();
    (void)Argc;
    (void)Argv;
    Put("mem: free=");
    PutU64(Free);
    Put(" pages (");
    PutU64(Free * 4u);
    Put(" KiB) / total=");
    PutU64(Total);
    Put("\n");
}

static void CmdPs(int Argc, char **Argv) {
    const char *Last = ProcessLastExecName();
    (void)Argc;
    (void)Argv;
    Put("pid  state  name\n");
    Put("1    run    shell\n");
    if (Last != 0 && Last[0] != 0) {
        Put("2    exit   ");
        Put(Last);
        Put("\n");
    }
}

static void CmdExec(int Argc, char **Argv) {
    if (Argc < 2) {
        Put("exec: need ELF\n");
        return;
    }
    (void)ProcessExecPath(Argv[1]);
}

void ShellSysRegister(void) {
    ShellCmdRegister("mem", "show free pages", CmdMem);
    ShellCmdRegister("ps", "list processes", CmdPs);
    ShellCmdRegister("exec", "run root ELF", CmdExec);
}
