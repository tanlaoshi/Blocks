/*
 * StreamStartupTone.c — 开机/Beep 内置柔和上升钟声（自研合成，非第三方采样）
 *
 * 缓冲与旧方波同形：整窗 Cap 填一音再 RunDma；多音连奏。勿用非整窗长度。
 */
#include "BootTypes.h"

#define STARTUP_TONE_RATE_HZ 48000u
#define STARTUP_TONE_NOTE_COUNT 4u
#define STARTUP_TONE_PEAK 24000
#define STARTUP_TONE_NOTE_REPEATS 5u

static const INT16 sSineTable[256] = {
    0,    804,   1608,   2410,   3212,   4011,   4808,   5602,  6393,   7179,
    7962,  8739,  9512,  10278, 11039, 11793, 12539, 13279, 14010, 14732,
    15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403,
    22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571,
    30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
    32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609, 32521,
    32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790,
    26319, 25832, 25329, 24811, 24279, 23731, 23170, 22594, 22005, 21403,
    20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732,
    14010, 13279, 12539, 11793, 11039, 10278, 9512,  8739,  7962,  7179,
    6393,  5602,  4808,  4011,  3212,  2410,  1608,  804,   0,    -804,
    -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739,
    -9512, -10278, -11039, -11793, -12539, -13279, -14010, -14732, -15446,
    -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403,
    -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319,
    -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137,
    -32285, -32412, -32521, -32609, -32678, -32728, -32757, -32767, -32757,
    -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785,
    -31580, -31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268,
    -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329,
    -24811, -24279, -23731, -23170, -22594, -22005, -21403, -20787, -20159,
    -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010,
    -13279, -12539, -11793, -11039, -10278, -9512,  -8739,  -7962,  -7179,
    -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,  -804,
};

/* C5 → E5 → G5 → C6 */
static const UINT32 sNoteHertz[STARTUP_TONE_NOTE_COUNT] = {523u, 659u, 784u,
                                                           1046u};

static void FillSoftNote(INT16 *Pcm, UINTN Frames, UINT32 Hertz, INT16 Peak) {
    UINTN Index;
    UINT32 Phase = 0;
    UINT32 Step;
    UINTN Attack;
    UINTN Release;

    if (Pcm == 0 || Frames == 0u || Hertz == 0u || Peak <= 0) {
        return;
    }
    Step = (UINT32)(((UINT64)Hertz << 16) / STARTUP_TONE_RATE_HZ);
    if (Step == 0u) {
        Step = 1u;
    }
    Attack = Frames / 10u;
    Release = Frames / 4u;
    if (Attack == 0u) {
        Attack = 1u;
    }
    if (Release == 0u) {
        Release = 1u;
    }
    for (Index = 0; Index < Frames; Index++) {
        INT32 Sample;
        INT32 Envelope = Peak;
        UINT32 TableIndex = (Phase >> 8) & 0xFFu;

        if (Index < Attack) {
            Envelope = (INT32)Peak * (INT32)(Index + 1u) / (INT32)Attack;
        } else if (Index + Release >= Frames) {
            Envelope = (INT32)Peak * (INT32)(Frames - Index) / (INT32)Release;
        }
        Sample = ((INT32)sSineTable[TableIndex] * Envelope) / 32767;
        Pcm[Index * 2u] = (INT16)Sample;
        Pcm[Index * 2u + 1u] = (INT16)Sample;
        Phase += Step;
    }
}

int HdaStreamPlayBuiltinTone(INT16 *Pcm, UINTN CapacityBytes,
                             int (*RunDmaCallback)(UINT32 PcmBytes)) {
    UINTN NoteIndex;
    UINTN Frames;
    UINTN Repeat;

    if (Pcm == 0 || CapacityBytes < 128u || RunDmaCallback == 0) {
        return 0;
    }
    /* 必须整窗 DMA（与旧方波同形）；长度对齐 128B */
    CapacityBytes &= ~(UINTN)127u;
    Frames = CapacityBytes / 4u;
    if (Frames == 0u) {
        return 0;
    }
    for (NoteIndex = 0; NoteIndex < STARTUP_TONE_NOTE_COUNT; NoteIndex++) {
        FillSoftNote(Pcm, Frames, sNoteHertz[NoteIndex], STARTUP_TONE_PEAK);
        for (Repeat = 0; Repeat < STARTUP_TONE_NOTE_REPEATS; Repeat++) {
            if (!RunDmaCallback((UINT32)CapacityBytes)) {
                return 0;
            }
        }
    }
    return 1;
}
