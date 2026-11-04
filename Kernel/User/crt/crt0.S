/* User/crt/crt0.S — _start → BlocksCrtCallMain → exit（syscall）
 * 栈布局：[rsp]=argc, [rsp+8]=argv…|NULL|envp…|NULL
 * 号段重排后 SYS_PROCESS_EXIT=50（勿再写死 0）
 */
#include <Blocks/syscall.h>
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern BlocksCrtCallMain
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    call BlocksCrtCallMain
    mov rdi, rax
    mov rax, SYS_PROCESS_EXIT
    xor rsi, rsi
    xor rdx, rdx
    syscall
1:
    jmp 1b
