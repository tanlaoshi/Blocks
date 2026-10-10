/*
 * Crt0.S — K51 薄 CRT：_start → main → exit(rax)
 */
    .text
    .globl _start
    .type  _start, @function
_start:
    call    main
    mov     %rax, %rdi
    mov     $2, %rax
    int     $0x80
1:
    jmp     1b
