#include <Blocks/Syscall.h>
/*
 * DynDemo — JUMP_SLOT(blocks_hello) + COPY(blocks_answer) + PT_TLS(blocks_tls)
 * 验收：串口 from.so / ans=42 / tls=7 / done
 */
    .intel_syntax noprefix
    .section .tdata,"awT",@progbits
    .global blocks_tls
    .type blocks_tls, @object
    .size blocks_tls, 8
blocks_tls:
    .quad 7
    .section .text
    .global _start
    .extern blocks_hello
    .extern blocks_answer
_start:
    call blocks_hello
    /* COPY 槽：绝对址读（ld 生成 R_X86_64_COPY） */
    mov rax, qword ptr [blocks_answer]
    cmp rax, 42
    jne fail
    /* Variant II：%fs:tpoff → .tdata */
    mov rdx, qword ptr fs:blocks_tls@tpoff
    cmp rdx, 7
    jne fail
    mov rax, SYS_FILE_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_ok]
    mov rdx, 14
    int 0x80
    mov rax, SYS_PROCESS_EXIT
    xor rdi, rdi
    int 0x80
fail:
    mov rax, SYS_FILE_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_fail]
    mov rdx, 5
    int 0x80
    mov rax, SYS_PROCESS_EXIT
    mov rdi, 1
    int 0x80

    .section .rodata
msg_ok:
    .ascii "ans+tls ok\n"
msg_fail:
    .ascii "fail\n"
