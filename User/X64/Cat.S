/*
 * Cat.S — K51：open/read/write 打印 BLOCKS.ID（CAT.ELF）
 * 缓冲在栈上（无 .bss，适配最小 ld）
 */
    .text
    .globl main
    .type  main, @function
main:
    sub     $80, %rsp
    mov     $9, %rax
    lea     path(%rip), %rdi
    xor     %rsi, %rsi
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail
    mov     %rax, %rbx

.Lloop:
    mov     $3, %rax
    mov     %rbx, %rdi
    mov     %rsp, %rsi
    mov     $64, %rdx
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail
    je      .Lclose
    mov     %rax, %rdx
    mov     $1, %rax
    mov     $1, %rdi
    mov     %rsp, %rsi
    int     $0x80
    jmp     .Lloop

.Lclose:
    mov     $4, %rax
    mov     %rbx, %rdi
    int     $0x80
    add     $80, %rsp
    xor     %rax, %rax
    ret

.Lfail:
    mov     $1, %rax
    mov     $1, %rdi
    lea     err(%rip), %rsi
    mov     $4, %rdx
    int     $0x80
    add     $80, %rsp
    mov     $1, %rax
    ret

    .section .rodata
path:
    .asciz  "BLOCKS.ID"
err:
    .ascii  "err\n"
