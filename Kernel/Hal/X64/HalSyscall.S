/*
 * HalSyscall.S — int 0x80 ISR
 *
 * 寄存器帧（从栈顶）：rax rbx rcx rdx rsi rdi rbp r8..r15
 * exit：丢帧，恢复 gSyscallRetRsp，跳 gSyscallRetRip（HalSyscallRun 尾）
 * fork：gForkUserRip/Rsp + gFork*Cr3（与 gSyscallRet* 分离）
 */
    .text
    .globl HalSyscallIsr
    .type  HalSyscallIsr, @function
HalSyscallIsr:
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax
    mov  %rsp, %rdi
    sub  $8, %rsp
    cld
    call HalSyscallDispatch
    add  $8, %rsp
    /* gUserDone：0=iret；1=结束 Run；2=回父；3=进子 */
    movabs $gUserDone, %rax
    cmpl   $3, (%rax)
    je     .LforkChild
    cmpl   $2, (%rax)
    je     .LforkParent
    cmpl   $0, (%rax)
    jne    .Lexit
    pop  %rax
    pop  %rbx
    pop  %rcx
    pop  %rdx
    pop  %rsi
    pop  %rdi
    pop  %rbp
    pop  %r8
    pop  %r9
    pop  %r10
    pop  %r11
    pop  %r12
    pop  %r13
    pop  %r14
    pop  %r15
    iretq

.LforkChild:
    movabs $gUserDone, %rax
    movl   $0, (%rax)
    movabs $gForkChildCr3, %rax
    mov    (%rax), %rax
    mov    %rax, %cr3
    movabs $gForkUserRsp, %rax
    mov    (%rax), %rsp
    movabs $gForkResumeRax, %rax
    mov    (%rax), %rax
    movabs $gForkUserRip, %r11
    jmp    *(%r11)

.LforkParent:
    movabs $gUserDone, %rax
    movl   $0, (%rax)
    movabs $gForkParentCr3, %rax
    mov    (%rax), %rax
    mov    %rax, %cr3
    movabs $gForkUserRsp, %rax
    mov    (%rax), %rsp
    movabs $gForkResumeRax, %rax
    mov    (%rax), %rax
    movabs $gForkUserRip, %r11
    jmp    *(%r11)

.Lexit:
    movabs $gSyscallRetRsp, %rax
    mov    (%rax), %rsp
    movabs $gSyscallRetRip, %rax
    jmp    *(%rax)
    .size HalSyscallIsr, .-HalSyscallIsr
