/*
 * KernelEntry.S — X64 内核 ELF 入口（对标 Arm/RiscV KernelEntry）
 *
 * EFI JumpToKernel 跳到这里：可能是 MS ABI（RCX=UEFI_BOOT_CONFIG*）
 * 或 SysV（RDI）。先换到 gEarlyStack，再 call KernelHandoff——C 侧不再改 rsp。
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
    leaq    gEarlyStack(%rip), %rsp
    addq    $65536, %rsp
    andq    $-16, %rsp
    xorq    %rbp, %rbp

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
