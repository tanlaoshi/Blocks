/*
 * HalSerialWrite.c — 通道过滤与写出（PR-S-halserial-1）
 */
#include "HalSerial.h"
#include "HalSerialPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Serial/Serial.h"
#include "Font.h"
#include "SerialConfig.h"
#include "SpinLock.h"
#include "XHCI.h"
#include "Ehci.h"

/* PR-TEST：串口输出锁。fork 后父子并发 printf 会字节交错（如 PIPEDEMO 的
 * "root=0x000PING000..."），用自旋锁把每次 HalSerialWriteChannel 调用做成
 * 原子（关中断，避免持锁被定时器打断再抢同锁死锁）。RingAppend/GopWrite
 * 不会回调本函数，无重入风险。零初始化即解锁态。 */
static SPIN_LOCK gSerialLock;
static int gShellOwnUart;
#define RT_LOG_MAX 16384u
static char gRtLog[RT_LOG_MAX];
static UINTN gRtLen;
static int gRtDirty;
/* 从第一条日志起保留，USB 转串口就绪后整段补发。64KB 盖住一次开机。 */
#define BOOT_CAPTURE_MAX (64u * 1024u)
static char gBootCapture[BOOT_CAPTURE_MAX];
static UINTN gBootCaptureLen;
static int gUsbReplayDone = 1;

void HalSerialBootCapture(const char *Text) {
    if (!Text) {
        return;
    }
    while (*Text) {
        if (gBootCaptureLen + 1u >= BOOT_CAPTURE_MAX) {
            UINTN Keep = (BOOT_CAPTURE_MAX / 4u) * 3u;
            UINTN i;
            for (i = 0; i < Keep; i++) {
                gBootCapture[i] = gBootCapture[gBootCaptureLen - Keep + i];
            }
            gBootCaptureLen = Keep;
        }
        gBootCapture[gBootCaptureLen++] = *Text++;
    }
}

static void TeeUsb(const char *Text) {
    if (!gUsbReplayDone || gShellOwnUart || !Text) {
        return;
    }
    if (gFtdiUart.Write) {
        gFtdiUart.Write(Text);
    }
    FtdiWrite(Text);
    if (gCdcUart.Write) {
        gCdcUart.Write(Text);
    }
}

static void RtAppend(const char *Text) {
    if (!gShellOwnUart || !Text) {
        return;
    }
    while (*Text) {
        if (gRtLen + 1 >= RT_LOG_MAX) {
            UINTN Keep = RT_LOG_MAX / 2u;
            UINTN i;
            for (i = 0; i < Keep; i++) {
                gRtLog[i] = gRtLog[gRtLen - Keep + i];
            }
            gRtLen = Keep;
        }
        gRtLog[gRtLen++] = *Text++;
        gRtDirty = 1;
    }
}

static int ChannelGopOn(int Channel) {
#if !SCREEN_LOG
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case SLOG_BOOT:
        return SCREEN_LOG_BOOT;
    case SLOG_USB:
        return SCREEN_LOG_USB;
    case SLOG_SMP:
        return SCREEN_LOG_SMP;
    case SLOG_GUI:
        return SCREEN_LOG_GUI;
    case SLOG_NET:
        return SCREEN_LOG_NET;
    case SLOG_FS:
        return SCREEN_LOG_FS;
    case SLOG_MEM:
        return SCREEN_LOG_MEM;
    case SLOG_DRV:
        return SCREEN_LOG_DRV;
    case SLOG_MISC:
    default:
        return SCREEN_LOG_MISC;
    }
#endif
}

static int ChannelUartOn(int Channel) {
#if !SERIAL_ENABLE
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case SLOG_BOOT:
        return SERIAL_BOOT;
    case SLOG_USB:
        return SERIAL_USB;
    case SLOG_SMP:
        return SERIAL_SMP;
    case SLOG_GUI:
        return SERIAL_GUI;
    case SLOG_NET:
        return SERIAL_NET;
    case SLOG_FS:
        return SERIAL_FS;
    case SLOG_MEM:
        return SERIAL_MEM;
    case SLOG_DRV:
        return SERIAL_DRV;
    case SLOG_MISC:
    default:
        return SERIAL_MISC;
    }
#endif
}

static void GopMirrorLine(const char *Text) {
    if (!Text || !gGopMirror || !gVideoUp || gGopMute) {
        return;
    }
    GopWrite(Text);
}

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    SpinLockAcquire(&gSerialLock);
    /* ring 始终收（boot / Desktop） */
    RingAppend(Text);
    RtAppend(Text);
    if (!gShellOwnUart && SerialPresent() && ChannelUartOn(Channel)) {
        SerialWrite(Text);
    }
    /* 屏：受 SCREEN_LOG_* + Mirror/Mute（有无 COM1 都可画） */
    if (ChannelGopOn(Channel)) {
        GopMirrorLine(Text);
    }
    SpinLockRelease(&gSerialLock);
    TeeUsb(Text);
}

void HalSerialShellOwn(void) {
    gShellOwnUart = 1;
}

int HalSerialShellOwned(void) {
    return gShellOwnUart;
}

int HalSerialRuntimeDirty(void) {
    return gRtDirty;
}

void HalSerialRuntimeMarkSaved(void) {
    gRtDirty = 0;
}

UINTN HalSerialRuntimeSnapshot(char *Dst, UINTN Max) {
    UINTN N;
    UINTN i;

    if (!Dst || Max == 0) {
        return 0;
    }
    SpinLockAcquire(&gSerialLock);
    N = gRtLen;
    if (N > Max) {
        N = Max;
    }
    for (i = 0; i < N; i++) {
        Dst[i] = gRtLog[i];
    }
    SpinLockRelease(&gSerialLock);
    return N;
}

UINTN HalSerialBootSnapshot(char *Dst, UINTN Max) {
    UINTN N;
    UINTN i;

    if (!Dst || Max == 0) {
        return 0;
    }
    SpinLockAcquire(&gSerialLock);
    N = gBootCaptureLen;
    if (N > Max) {
        N = Max;
    }
    for (i = 0; i < N; i++) {
        Dst[i] = gBootCapture[i];
    }
    SpinLockRelease(&gSerialLock);
    return N;
}

void HalSerialReplayUsb(void) {
    char Chunk[128];
    UINTN Off = 0;

    for (;;) {
        UINTN N = 0;

        SpinLockAcquire(&gSerialLock);
        while (Off < gBootCaptureLen && N + 1u < sizeof(Chunk)) {
            Chunk[N++] = gBootCapture[Off++];
        }
        SpinLockRelease(&gSerialLock);
        if (N == 0) {
            break;
        }
        Chunk[N] = 0;
        if (gFtdiUart.Write) {
            gFtdiUart.Write(Chunk);
        }
        FtdiWrite(Chunk);
        if (gCdcUart.Write) {
            gCdcUart.Write(Chunk);
        }
    }
    gUsbReplayDone = 1;
}

void HalSerialWriteShell(const char *Text) {
    if (!Text) {
        return;
    }
    SpinLockAcquire(&gSerialLock);
    RingAppend(Text);
    if (SerialPresent()) {
        SerialWrite(Text);
    }
    SpinLockRelease(&gSerialLock);
    if (gFtdiUart.Write) {
        gFtdiUart.Write(Text);
    }
    FtdiWrite(Text);
    if (gCdcUart.Write) {
        gCdcUart.Write(Text);
    }
}

void HalSerialWrite(const char *Text) {
    HalSerialWriteChannel(SLOG_MISC, Text);
}

void HalSerialWriteChannelHex32(int Channel, UINT32 Value) {
    char Buf[12];

    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWriteChannel(Channel, Buf);
}

void HalSerialWriteChannelHex64(int Channel, UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWriteChannel(Channel, Buf);
}

/*
 * 真机 boot 进度：ring 始终；UART 受 SERIAL_*；屏受 SCREEN_LOG_*。
 * USB BootMark：Mute 期间也上屏（front only）。
 */
void HalSerialBootMarkChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    /* 与 WriteChannel 同锁：多核 BootLog 否则字节交错成 BBoot: */
    SpinLockAcquire(&gSerialLock);
    RingAppend(Text);
    RtAppend(Text);
    if (!gShellOwnUart && SerialPresent() && ChannelUartOn(Channel)) {
        SerialWrite(Text);
    }
    if (ChannelGopOn(Channel) && gGopMirror && gVideoUp) {
        /* 绕过 gGopMute：里程碑必须看得见；细日志走 Log* → GopMirrorLine 仍受 Mute */
        GopWrite(Text);
    }
    SpinLockRelease(&gSerialLock);
    TeeUsb(Text);
}

void HalSerialBootMark(const char *Text) {
    HalSerialBootMarkChannel(SLOG_BOOT, Text);
}

/* 串口里程碑。屏幕总开关关掉时不往 GOP 画。 */
void HalSerialBootMarkForce(const char *Text) {
    if (!Text) {
        return;
    }
    SpinLockAcquire(&gSerialLock);
    RingAppend(Text);
    RtAppend(Text);
    if (!gShellOwnUart && SerialPresent() && ChannelUartOn(SLOG_USB)) {
        SerialWrite(Text);
    }
    SpinLockRelease(&gSerialLock);
    TeeUsb(Text);
}
