/*
 * HalFpuSse.c — K27：仅此 TU 允许 SSE（build.sh CFLAGS_FPU）
 * 必须在 HalFpuBegin 内调用，否则 #UD。
 *
 * 【初学者】
 * - Hal/X64：FontTtf 栅格化前 HalFpuSelfTest 探针。
 * - 入口：HalFpuSseProbe。
 */
int HalFpuSseProbe(void) {
    volatile float A;
    volatile float B;
    float C;

    A = 1.5f;
    B = 2.0f;
    C = A * B;
    return (int)(C * 10.0f + 0.5f);
}
