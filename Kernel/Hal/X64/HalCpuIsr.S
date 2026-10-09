/*
 * HalCpuIsr.S — IDT 入口
 *
 * HalCpuIsrHalt：未处理异常/IRQ → 停机
 * HalCpuIsrTimer：K18 LAPIC timer → HalTimerIrq → iretq
 */
    .text
    .globl HalCpuIsrHalt
    .type  HalCpuIsrHalt, @function
HalCpuIsrHalt:
1:
    cli
    hlt
    jmp 1b
    .size HalCpuIsrHalt, .-HalCpuIsrHalt

    .globl HalCpuIsrTimer
    .type  HalCpuIsrTimer, @function
HalCpuIsrTimer:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    /* 中断帧后 15 push → call 前须 RSP≡8 (mod 16) */
    push %rax
    cld
    call HalTimerIrq
    pop %rax
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    iretq
    .size HalCpuIsrTimer, .-HalCpuIsrTimer
