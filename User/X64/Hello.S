/*
 * Hello.S — K19 最小用户程序（HELLO.ELF）
 *
 * int 0x80：rax=1 write(rdi=fd, rsi=buf, rdx=len)；rax=2 exit(rdi=code)
 */
    .text
    .globl _start
    .type  _start, @function
_start:
    mov     $1, %rax
    mov     $1, %rdi
    lea     msg(%rip), %rsi
    mov     $msg_end - msg, %rdx
    int     $0x80

    mov     $2, %rax
    xor     %rdi, %rdi
    int     $0x80
1:
    jmp     1b

    .section .rodata
msg:
    .ascii  "Hello from HELLO.ELF\n"
msg_end:
