#include <Blocks/syscall.h>
/* user/DynDemo.S — 动态链接演示：调用 LIBTOY.SO 中的 blocks_hello */
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern blocks_hello
_start:
    call blocks_hello
    mov rax, SYS_FILE_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_done]
    mov rdx, 5
    int 0x80
    mov rax, SYS_PROCESS_EXIT
    xor rdi, rdi
    int 0x80

    .section .rodata
msg_done:
    .ascii "done\n"
