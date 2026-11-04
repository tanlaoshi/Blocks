/*
 * ProcessArgEnvStack.c — execve 用户栈：argc / argv / envp
 *
 * 布局（低→高）：argc | argv[] | NULL | envp[] | NULL | 字符串区（自高向低铺）
 */
#include "ProcessPrivate.h"
#include "VirtualMemory.h"

int ProcessSetupArgEnvStack(VIRTUAL_ADDRESS_SPACE *Space, UINT64 StackTop,
                            char ArgBuf[][EXEC_ARG_LEN], int Argc,
                            char EnvBuf[][EXEC_ARG_LEN], int Envc,
                            UINT64 *OutRsp) {
    UINT64 Sp = StackTop;
    UINT64 ArgPtrs[EXEC_ARGV_MAX];
    UINT64 EnvPtrs[EXEC_ENV_MAX];
    UINT64 PtrSlot;
    UINT64 ArgcSlot;
    UINT64 Need;
    int Index;
    UINTN Length;

    if (!Space || !OutRsp || Argc < 0 || Argc > EXEC_ARGV_MAX || Envc < 0 ||
        Envc > EXEC_ENV_MAX) {
        return -1;
    }
    if (Argc > 0 && !ArgBuf) {
        return -1;
    }
    if (Envc > 0 && !EnvBuf) {
        return -1;
    }

    for (Index = Envc - 1; Index >= 0; Index--) {
        Length = 0;
        while (EnvBuf[Index][Length] && Length + 1 < EXEC_ARG_LEN) {
            Length++;
        }
        Length++;
        Sp = (Sp - Length) & ~7ULL;
        if (VirtualMemoryCopyToSpace(Space, Sp, EnvBuf[Index], Length) < 0) {
            return -1;
        }
        EnvPtrs[Index] = Sp;
    }
    for (Index = Argc - 1; Index >= 0; Index--) {
        Length = 0;
        while (ArgBuf[Index][Length] && Length + 1 < EXEC_ARG_LEN) {
            Length++;
        }
        Length++;
        Sp = (Sp - Length) & ~7ULL;
        if (VirtualMemoryCopyToSpace(Space, Sp, ArgBuf[Index], Length) < 0) {
            return -1;
        }
        ArgPtrs[Index] = Sp;
    }

    /* argc + argv[]+NULL + envp[]+NULL；整体 16 对齐 */
    Need = 8ULL * (UINT64)(1 + Argc + 1 + Envc + 1);
    Sp = (Sp - Need) & ~0xFULL;
    ArgcSlot = Sp;
    PtrSlot = Sp + 8;
    {
        UINT64 ArgCount = (UINT64)(UINT32)Argc;
        UINT64 Zero = 0;

        if (VirtualMemoryCopyToSpace(Space, ArgcSlot, &ArgCount, 8) < 0) {
            return -1;
        }
        for (Index = 0; Index < Argc; Index++) {
            if (VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)Index,
                                        &ArgPtrs[Index], 8) < 0) {
                return -1;
            }
        }
        if (VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)Argc, &Zero,
                                    8) < 0) {
            return -1;
        }
        for (Index = 0; Index < Envc; Index++) {
            if (VirtualMemoryCopyToSpace(
                    Space, PtrSlot + 8ULL * (UINT64)(Argc + 1 + Index),
                    &EnvPtrs[Index], 8) < 0) {
                return -1;
            }
        }
        if (VirtualMemoryCopyToSpace(
                Space, PtrSlot + 8ULL * (UINT64)(Argc + 1 + Envc), &Zero, 8) <
            0) {
            return -1;
        }
    }

    *OutRsp = ArgcSlot;
    return 0;
}
