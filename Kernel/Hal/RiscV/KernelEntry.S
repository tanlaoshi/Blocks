/*
 * KernelEntry.S — RiscV QEMU virt 入口（OpenSBI / -kernel）
 *
 * 【初学者 · 调用约定】
 *   a0 = hartid（硬件线程号）
 *   a1 = DTB 物理地址
 *
 * 【为何比 Arm64 复杂？】
 *   多核时 OpenSBI 的「Boot HART」经常不是 hart 0（例如 -smp 2 时可能是 hart 1）。
 *   若写成「只有 hartid==0 当 BSP」会选错核甚至死锁。
 *   做法：用原子 amoswap 抢 gBspClaimed——谁先到谁当 BSP；其余进 SecondaryPark。
 *
 * BSP：架栈 → 清 BSS → KernelHandoff(hartid, dtb) →（正常不返回）wfi
 * AP ：不清 BSS；轮询 gApGo[hartid]，再进 HalApMain（见 SmpStub / 以后真 SMP）
 */
    .section .data
    .align 3
    .global gBspClaimed
gBspClaimed:
    .word 0                 /* 0=尚无 BSP；amoswap 写成 1 表示已抢走 */
    .global gBspHartId
gBspHartId:
    .dword 0                /* 胜出的硬件 hartid，供调试与 SMP 核对 */

    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, @function
KernelEntry:
    /* 保住 OpenSBI 传来的参数（后面会改 a0/a1/t*） */
    mv      s0, a0          /* s0 = hartid */
    mv      s1, a1          /* s1 = DTB */

    /*
     * 竞选 BSP：原子交换 gBspClaimed = 1，旧值在 t2。
     * t2!=0 → 已有人当选 → 本 hart 是次核 → SecondaryPark
     * gBspClaimed 放在 .data（非 BSS），清 BSS 前即可读。
     */
    lla     t0, gBspClaimed
    li      t1, 1
    amoswap.w.aq t2, t1, (t0)
    bnez    t2, SecondaryPark

    /* 记录 BSP 的 hartid */
    lla     t0, gBspHartId
    sd      s0, 0(t0)

    /* 临时栈：gBootStack 基址 + 64KiB */
    lla     sp, gBootStack
    li      t0, 65536
    add     sp, sp, t0

    /* 清 BSS：static BOOT_INFO 等依赖为零 */
    lla     t0, __bss_start
    lla     t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    /* C ABI：a0=hartid，a1=DTB → void KernelHandoff(UINT64 HartId, UINT64 DtbPhys) */
    mv      a0, s0
    mv      a1, s1
    call    KernelHandoff

3:  wfi
    j       3b

/*
 * SecondaryPark — 同 payload 一起进来、但未抢到 BSP 的 hart
 *
 * 等待 Kernel SMP 写下 gApGo[hartid] == 'A14G'（SMP_GO_MAGIC），
 * 再读 gApLogical[hartid] 得到逻辑 CPU 号，用 gApStackTop[逻辑号] 作栈，
 * 调用 HalApMain(逻辑 CPU)。
 *
 * 勿清 BSS、勿调 KernelHandoff（那些只能 BSP 做一次）。
 * 经 SBI HSM 另拉起的 hart 走 HalApEntry，不经过本路径。
 */
SecondaryPark:
    lla     t0, gApGo
    slli    t1, s0, 2       /* hartid * 4：每槽一个 word */
    add     t0, t0, t1
    li      t2, 0x41313447  /* 小端 ASCII：'A''1''4''G' */
4:  lw      t1, 0(t0)
    beq     t1, t2, 5f
    wfi
    j       4b
5:
    /* s1 ← 逻辑 CPU 编号 */
    lla     t0, gApLogical
    slli    t1, s0, 2
    add     t0, t0, t1
    lw      s1, 0(t0)

    /* SP ← gApStackTop[logical]（每核一栈，元素为 8 字节指针） */
    lla     t0, gApStackTop
    slli    t1, s1, 3
    add     t0, t0, t1
    ld      sp, 0(t0)

    mv      a0, s1
    call    HalApMain

6:  wfi
    j       6b
    .size KernelEntry, .-KernelEntry
