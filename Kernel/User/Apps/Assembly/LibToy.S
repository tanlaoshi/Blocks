#include <Blocks/Syscall.h>
/* user/LibToy.S — 共享库：blocks_hello + blocks_answer（供 R_*_COPY） */
    .intel_syntax noprefix
    .section .data
    .global blocks_answer
    .type blocks_answer, @object
    .size blocks_answer, 8
blocks_answer:
    .quad 42
    .section .rodata
blocks_msg:
    .ascii "from.so\n"
    .section .text
    .global blocks_hello
blocks_hello:
    mov rax, SYS_FILE_WRITE
    mov rdi, 1
    lea rsi, [rip + blocks_msg]
    mov rdx, 8
    int 0x80
    ret
