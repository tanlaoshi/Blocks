#include <Blocks/syscall.h>
/* user/NetDemo.S — socket/connect/write/read/close via syscalls (needs LWIP=1) */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    sub rsp, 80                 /* recv buffer on stack */

    /* socket(AF_INET=2, SOCK_STREAM=1, 0) */
    mov rax, SYS_NET_SOCKET
    mov rdi, 2
    mov rsi, 1
    xor rdx, rdx
    int 0x80
    cmp rax, 0
    jl .Lfail
    mov rbx, rax                /* fd */

    /* connect(fd, 10.0.2.2, 8888) */
    mov rax, SYS_NET_CONNECT
    mov rdi, rbx
    mov rsi, 0x0A000202         /* 10.0.2.2 host-order */
    mov rdx, 8888
    int 0x80
    cmp rax, 0
    jl .Lfail

    /* FileWrite(fd, msg, len) */
    mov rax, SYS_FILE_WRITE
    mov rdi, rbx
    lea rsi, [rip + msg]
    mov rdx, 12
    int 0x80
    cmp rax, 0
    jle .Lfail

    /* FileRead(fd, buf, 64) — optional reply from host nc */
    mov rax, SYS_FILE_READ
    mov rdi, rbx
    mov rsi, rsp
    mov rdx, 64
    int 0x80
    mov rcx, rax                /* nbytes (may be 0) */

    /* FileClose(fd) */
    mov rax, SYS_FILE_CLOSE
    mov rdi, rbx
    int 0x80

    /* stdout: ok\n */
    mov rax, SYS_FILE_WRITE
    mov rdi, 1
    lea rsi, [rip + ok]
    mov rdx, 3
    int 0x80

    /* if read got data, echo it */
    cmp rcx, 0
    jle .Lexit
    mov rax, SYS_FILE_WRITE
    mov rdi, 1
    mov rsi, rsp
    mov rdx, rcx
    int 0x80

.Lexit:
    mov rax, SYS_PROCESS_EXIT
    xor rdi, rdi
    int 0x80

.Lfail:
    mov rax, SYS_FILE_WRITE
    mov rdi, 1
    lea rsi, [rip + err]
    mov rdx, 4
    int 0x80
    mov rax, SYS_PROCESS_EXIT
    mov rdi, 1
    int 0x80

    .section .rodata
msg:
    .ascii "from ring3!\n"
ok:
    .ascii "ok\n"
err:
    .ascii "err\n"
