/* User/crt/crt0.S — _start → blocks_call_main → exit（syscall）
 * 栈布局：[rsp]=argc, [rsp+8]=argv…|NULL|envp…|NULL
 * 号段重排后 SYS_PROCESS_EXIT=50（勿再写死 0）
 */
#include <Blocks/syscall.h>
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern blocks_call_main
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    call blocks_call_main
    mov rdi, rax
    mov rax, SYS_PROCESS_EXIT
    xor rsi, rsi
    xor rdx, rdx
    syscall
1:
    jmp 1b
