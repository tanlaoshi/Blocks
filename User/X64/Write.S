/*
 * Write.S — K51：open(写)/write/close 写出 NOTE.TXT（WRITE.ELF）
 *
 * open flags=1 写/建
 */
    .text
    .globl main
    .type  main, @function
main:
    mov     $9, %rax
    lea     path(%rip), %rdi
    mov     $1, %rsi
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail
    mov     %rax, %rbx

    mov     $1, %rax
    mov     %rbx, %rdi
    lea     msg(%rip), %rsi
    mov     $msg_end - msg, %rdx
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail

    mov     $4, %rax
    mov     %rbx, %rdi
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail

    mov     $1, %rax
    mov     $1, %rdi
    lea     ok(%rip), %rsi
    mov     $3, %rdx
    int     $0x80
    xor     %rax, %rax
    ret

.Lfail:
    mov     $1, %rax
    mov     $1, %rdi
    lea     err(%rip), %rsi
    mov     $4, %rdx
    int     $0x80
    mov     $1, %rax
    ret

    .section .rodata
path:
    .asciz  "NOTE.TXT"
msg:
    .ascii  "hello-from-write\n"
msg_end:
ok:
    .ascii  "ok\n"
err:
    .ascii  "err\n"
