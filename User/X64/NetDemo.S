/*
 * NetDemo.S — K43：用户态 TCP 客户端（对标现网 NETDEMO）
 *
 * 需 Guest `lwip on`；宿主机：nc -l -p 8888
 * Shell：exec NETDEMO.ELF
 *
 * syscall：1 write · 2 exit · 3 read · 4 close · 5 socket · 6 connect
 */
    .text
    .globl _start
    .type  _start, @function
_start:
    sub     $80, %rsp

    /* socket(AF_INET=2, SOCK_STREAM=1, 0) */
    mov     $5, %rax
    mov     $2, %rdi
    mov     $1, %rsi
    xor     %rdx, %rdx
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail
    mov     %rax, %rbx

    /* connect(fd, 10.0.2.2, 8888) */
    mov     $6, %rax
    mov     %rbx, %rdi
    mov     $0x0A000202, %rsi
    mov     $8888, %rdx
    int     $0x80
    cmp     $0, %rax
    jl      .Lfail

    /* write(fd, msg, len) */
    mov     $1, %rax
    mov     %rbx, %rdi
    lea     msg(%rip), %rsi
    mov     $12, %rdx
    int     $0x80
    cmp     $0, %rax
    jle     .Lfail

    /* read(fd, buf, 64) */
    mov     $3, %rax
    mov     %rbx, %rdi
    mov     %rsp, %rsi
    mov     $64, %rdx
    int     $0x80
    mov     %rax, %rcx

    /* close(fd) */
    mov     $4, %rax
    mov     %rbx, %rdi
    int     $0x80

    /* stdout: ok\n */
    mov     $1, %rax
    mov     $1, %rdi
    lea     ok(%rip), %rsi
    mov     $3, %rdx
    int     $0x80

    cmp     $0, %rcx
    jle     .Lexit
    mov     $1, %rax
    mov     $1, %rdi
    mov     %rsp, %rsi
    mov     %rcx, %rdx
    int     $0x80

.Lexit:
    mov     $2, %rax
    xor     %rdi, %rdi
    int     $0x80

.Lfail:
    mov     $1, %rax
    mov     $1, %rdi
    lea     err(%rip), %rsi
    mov     $4, %rdx
    int     $0x80
    mov     $2, %rax
    mov     $1, %rdi
    int     $0x80

    .section .rodata
msg:
    .ascii  "from ring3!\n"
ok:
    .ascii  "ok\n"
err:
    .ascii  "err\n"
