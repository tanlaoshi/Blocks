#include <Blocks/syscall.h>
/* user/CatFile.S — 旧 int 0x80 对照；CRT 版见 Cat.c（CAT.ELF） */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, SYS_FILE_OPEN              /* SYS_FILE_OPEN */
    lea rdi, [rip + path]
    int 0x80
    cmp rax, 0
    jl .Lfail
    mov rbx, rax            /* fd */

.Lloop:
    mov rax, SYS_FILE_READ              /* SYS_FILE_READ */
    mov rdi, rbx
    lea rsi, [rsp - 64]
    mov rdx, 32
    int 0x80
    cmp rax, 0
    jle .Lclose
    mov rdx, rax
    mov rax, SYS_FILE_WRITE              /* SYS_FILE_WRITE */
    mov rdi, 1
    lea rsi, [rsp - 64]
    int 0x80
    jmp .Lloop

.Lclose:
    mov rax, SYS_FILE_CLOSE              /* SYS_FILE_CLOSE */
    mov rdi, rbx
    int 0x80
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
path:
    .asciz "BLOCKS.ID"
err:
    .ascii "err\n"
