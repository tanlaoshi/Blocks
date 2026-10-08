/*
 * KernelEntry.S — X64 内核 ELF 的第一行汇编
 *
 * 【初学者 · 谁跳到这里？】
 * ToyBoot（BOOTX64.EFI）在 ExitBootServices 之后，按 ELF 入口跳进来，
 * 并递上 UEFI_BOOT_CONFIG*。调用约定可能是：
 *   - Microsoft x64 ABI：第一个参数在 RCX
 *   - SysV（偶发）：第一个参数在 RDI
 * 所以下面两种都试一下。
 *
 * 【为什么一进来就换栈？】
 * UEFI 给的栈往往在很高的物理地址。我们马上要开自己的 4GiB 恒等页表，
 * 高址栈会「掉出窗口」。于是先用本文件 BSS 里的 gEarlyStack（64KiB）。
 *
 * 【然后呢？】
 * call KernelHandoff(config) —— 纯 C，负责译成 BOOT_INFO 再进 KernelMain。
 * 若意外返回：hlt 死循环，避免跑飞。
 *
 * 符号 KernelEntry 必须与链接脚本 ENTRY() 一致。
 */
    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, @function
KernelEntry:
    /* 保住交接指针：优先 RCX，空则用 RDI */
    movq    %rcx, %r8
    testq   %r8, %r8
    jnz     1f
    movq    %rdi, %r8
1:
    /* RSP → gEarlyStack 顶端，并 16 字节对齐（ABI 要求） */
    leaq    gEarlyStack(%rip), %rsp
    addq    $65536, %rsp
    andq    $-16, %rsp
    xorq    %rbp, %rbp

    /* 同时满足 SysV(RDI) 与 MS(RCX)，KernelHandoff 只吃一个指针 */
    movq    %r8, %rdi
    movq    %r8, %rcx
    call    KernelHandoff

2:  hlt
    jmp     2b
    .size KernelEntry, .-KernelEntry

    .section .bss
    .align 16
    .global gEarlyStack
gEarlyStack:
    .space 65536
