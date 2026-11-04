/* User/crt/syscall.S — long blocks_syscall(n, a, b, c) via syscall */
    .intel_syntax noprefix
    .section .text
    .global blocks_syscall
blocks_syscall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    syscall
    ret

    .global blocks_syscall2
    .set blocks_syscall2, blocks_syscall
