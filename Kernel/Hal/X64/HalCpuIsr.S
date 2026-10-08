/*
 * HalCpuIsr.S — K7：所有 IDT 门先指向此处
 *
 * 本刀不开中断（不 sti）。若误入异常，停在 cli;hlt，避免乱跑。
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
