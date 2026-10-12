/*
 * KernelEntry.S — BringUp/X64：内核 ELF 入口（HAL/X64/link.ld ENTRY）
 *
 * Boot ExitBootServices 后跳到这里（RCX 或 RDI = UEFI_BOOT_CONFIG*）。
 * 换到 gEarlyStack → KernelHandoff → BringUp/KernelMain。
 */
    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, @function
KernelEntry:
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
