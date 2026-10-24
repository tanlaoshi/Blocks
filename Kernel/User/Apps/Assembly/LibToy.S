#include <Blocks/syscall.h>
/* user/LibToy.S — 共享库：导出 blocks_hello，打印 from.so */
    .intel_syntax noprefix
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
