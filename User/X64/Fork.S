/*
 * Fork.S — K50：fork + wait 课表演示（FORK.ELF）
 *
 * rax=7 fork；子打印 C\\n 后 exit(42)；父打印 P\\n、wait、done\\n、exit(0)
 */
    .text
    .globl _start
    .type  _start, @function
_start:
    mov     $7, %rax
    int     $0x80
    cmp     $0, %rax
    je      .Lchild
    jl      .Lfail

    /* parent */
    mov     $1, %rax
    mov     $1, %rdi
    lea     msg_p(%rip), %rsi
    mov     $2, %rdx
    int     $0x80

    mov     $8, %rax
    xor     %rdi, %rdi
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail

    mov     $1, %rax
    mov     $1, %rdi
    lea     msg_done(%rip), %rsi
    mov     $5, %rdx
    int     $0x80

    mov     $2, %rax
    xor     %rdi, %rdi
    int     $0x80
1:
    jmp     1b

.Lchild:
    mov     $1, %rax
    mov     $1, %rdi
    lea     msg_c(%rip), %rsi
    mov     $2, %rdx
    int     $0x80
    mov     $2, %rax
    mov     $42, %rdi
    int     $0x80
2:
    jmp     2b

.Lfail:
    mov     $1, %rax
    mov     $1, %rdi
    lea     msg_fail(%rip), %rsi
    mov     $5, %rdx
    int     $0x80
    mov     $2, %rax
    mov     $1, %rdi
    int     $0x80
3:
    jmp     3b

    .section .rodata
msg_p:
    .ascii  "P\n"
msg_c:
    .ascii  "C\n"
msg_done:
    .ascii  "done\n"
msg_fail:
    .ascii  "fail\n"
