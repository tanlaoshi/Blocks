/*
 * JumpToKernel.c — Boot 第 6 步：GetMemoryMap、ExitBootServices、跳转 Kernel
 */
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>

#include "BootPrivate.h"

typedef VOID (*KERNEL_ENTRY_FN)(UEFI_BOOT_CONFIG *);

/*
 * 内核入口返回后的落点。
 *
 * 正常路径永远不该走到这里。但调试期很常见：入口被临时换成一个 ret 桩、
 * 或内核初始化提前 return。这时既不能 __builtin_unreachable()（未定义行为，
 * 编译器可以随便优化，实测表现是机器跑飞、只能断电），也不能回到上面的重试
 * 循环继续 ExitBootServices（Boot Services 已经没了）。
 *
 * 所以：打一行可读的日志，关中断，然后原地停住。
 * BootSerialPrintf 是直接读写 0x3F8，不经过 gBS，ExitBootServices 之后仍然合法。
 */
STATIC VOID KernelReturned(UINT64 EntryAddress) {
    DisableInterrupts();
    BootSerialPrintf("Boot: Kernel Returned at 0x%lx -- halting.\n", EntryAddress);
    BootSerialPrintf("Boot: This means the kernel entry returned; power cycle required.\n");
    CpuDeadLoop();
}

EFI_STATUS JumpToKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig) {
    EFI_STATUS Status;
    MEMORY_MAP MemoryMap = {NULL, 0, 0, 0, 0};
    UINTN MapKey = 0;
    UINTN Tries;
    UINTN Needed;

    BootSerialPrintf("Boot: JumpToKernel Entry=0x%lx\n",
                     (UINT64)BootConfig->EntryAddress);

    Status = gBS->GetMemoryMap(&MemoryMap.MapSize, NULL, &MapKey,
                               &MemoryMap.DescriptorSize, &MemoryMap.DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        return Status;
    }

    MemoryMap.MapSize += MemoryMap.DescriptorSize * 16;
    Status = gBS->AllocatePool(EfiLoaderData, MemoryMap.MapSize, &MemoryMap.Buffer);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    for (Tries = 0; Tries < 8; Tries++) {
        Needed = MemoryMap.MapSize;
        Status = gBS->GetMemoryMap(&Needed, (EFI_MEMORY_DESCRIPTOR *)MemoryMap.Buffer,
                                   &MapKey, &MemoryMap.DescriptorSize,
                                   &MemoryMap.DescriptorVersion);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            gBS->FreePool(MemoryMap.Buffer);
            MemoryMap.MapSize = Needed + MemoryMap.DescriptorSize * 16;
            Status = gBS->AllocatePool(EfiLoaderData, MemoryMap.MapSize, &MemoryMap.Buffer);
            if (EFI_ERROR(Status)) {
                return Status;
            }
            continue;
        }
        if (EFI_ERROR(Status)) {
            gBS->FreePool(MemoryMap.Buffer);
            return Status;
        }
        MemoryMap.MapSize = Needed;

        Status = gBS->ExitBootServices(ImageHandle, MapKey);
        if (!EFI_ERROR(Status)) {
            KERNEL_ENTRY_FN Entry;

            BootConfig->MemoryMap = MemoryMap;
            Entry = (KERNEL_ENTRY_FN)(UINTN)BootConfig->EntryAddress;
            /*
             * UEFI/MS ABI：首参在 RCX；HAL 入口优先读 RCX。
             *
             * PR-15 契约：BootConfig 指向 UefiMain 的栈（见 Boot.c 处注释）。
             * 这里传出后 Boot 不再使用它；内核必须在换栈前不碰它、并在
             * KernelHandoff 里整包复制。改这里的调用约定前先读 KernelHandoff.c。
             */
            Entry(BootConfig);
            KernelReturned(BootConfig->EntryAddress);
        }
    }

    BootSerialPrintf("ExitBootServices Failed After Retries: %r\n", Status);
    gBS->FreePool(MemoryMap.Buffer);
    return Status;
}
