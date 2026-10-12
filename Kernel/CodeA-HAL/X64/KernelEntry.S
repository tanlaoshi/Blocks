/*
 * KernelEntry.S — X64 内核 ELF 入口（link.ld ENTRY）
 *
 * Blocks Boot（BOOTX64.EFI）ExitBootServices 后跳到这里，递上 UEFI_BOOT_CONFIG*：
 *   - MS x64 ABI：RCX
 *   - SysV：RDI
 * 两种都认。立刻换到 BSS 里的 gEarlyStack（64KiB），离开可能很高的 UEFI 栈
 *（随后 KernelMain 开 4GiB EarlyIdentity）。
 *
 * 然后 call KernelHandoff；若返回则 hlt 死循环。
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
