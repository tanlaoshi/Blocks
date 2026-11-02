/*
 * Entry.S — Duo BootStub：U-Boot bootm 跳入时 a0=hartid、a1=DTB
 */
    .section .text.boot, "ax"
    .globl BootStubEntry
    .type BootStubEntry, @function
BootStubEntry:
    la   sp, gBootStubStackTop
    andi sp, sp, -16
    /* 清 BSS */
    la   t0, __bss_start
    la   t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd   zero, 0(t0)
    addi t0, t0, 8
    j    1b
2:
    call BootStubMain
    /* 正常不返回 */
3:
    wfi
    j    3b
    .size BootStubEntry, .-BootStubEntry
