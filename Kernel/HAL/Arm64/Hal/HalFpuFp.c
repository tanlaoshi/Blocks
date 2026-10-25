/*
 * HalFpuFp.c — 仅此 TU 允许 FP（Makefile CFLAGS_FPU，无 -mgeneral-regs-only）
 * 须在 HalFpuBegin 内调用。
 */
int HalFpuFpProbe(void) {
    volatile float A;
    volatile float B;
    float C;

    A = 1.5f;
    B = 2.0f;
    C = A * B;
    return (int)(C * 10.0f + 0.5f);
}
