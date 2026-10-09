/*
 * SerialConfig.h — 串口 / 屏上日志的编译期开关
 *
 * 【初学者 · 为什么要这么多开关？】
 * 开机日志若全开，串口和屏幕会被 USB/SMP/MEM 细节刷爆，反而不好看清主线。
 * 所以分两层：
 *   SERIAL_ENABLE      — 总闸：0=完全不碰 UART
 *   SERIAL_CH_BOOT/USB/… — 分模块：总闸开着时，某模块仍可 quiet
 * 屏幕同理：SCREEN_LOG 与 SCREEN_LOG_*（屏上滚动日志用）。
 *
 * 构建：
 *   ./build.sh x64              # 默认 SERIAL_ENABLE=1
 *   ./build.sh x64 SERIAL=0     # 关掉 UART
 *
 * 通道号 SLOG_* 给 HalSerialWriteChannel 用。
 */
#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#ifndef SERIAL_ENABLE
#define SERIAL_ENABLE 0
#endif

#if !SERIAL_ENABLE
#ifdef SERIAL_CH_BOOT
#undef SERIAL_CH_BOOT
#endif
#ifdef SERIAL_CH_USB
#undef SERIAL_CH_USB
#endif
#ifdef SERIAL_CH_SMP
#undef SERIAL_CH_SMP
#endif
#ifdef SERIAL_CH_GUI
#undef SERIAL_CH_GUI
#endif
#ifdef SERIAL_CH_NET
#undef SERIAL_CH_NET
#endif
#ifdef SERIAL_CH_FS
#undef SERIAL_CH_FS
#endif
#ifdef SERIAL_CH_MEM
#undef SERIAL_CH_MEM
#endif
#ifdef SERIAL_CH_DRV
#undef SERIAL_CH_DRV
#endif
#ifdef SERIAL_CH_MISC
#undef SERIAL_CH_MISC
#endif
#define SERIAL_CH_BOOT 0
#define SERIAL_CH_USB  0
#define SERIAL_CH_SMP  0
#define SERIAL_CH_GUI  0
#define SERIAL_CH_NET  0
#define SERIAL_CH_FS   0
#define SERIAL_CH_MEM  0
#define SERIAL_CH_DRV  0
#define SERIAL_CH_MISC 0
#else
#ifndef SERIAL_CH_BOOT
#define SERIAL_CH_BOOT 1
#endif
#ifndef SERIAL_CH_USB
#define SERIAL_CH_USB 1
#endif
#ifndef SERIAL_CH_SMP
#define SERIAL_CH_SMP 1
#endif
#ifndef SERIAL_CH_GUI
#define SERIAL_CH_GUI 1
#endif
#ifndef SERIAL_CH_NET
#define SERIAL_CH_NET 1
#endif
#ifndef SERIAL_CH_FS
#define SERIAL_CH_FS 1
#endif
#ifndef SERIAL_CH_MEM
#define SERIAL_CH_MEM 1
#endif
#ifndef SERIAL_CH_DRV
#define SERIAL_CH_DRV 1
#endif
#ifndef SERIAL_CH_MISC
#define SERIAL_CH_MISC 1
#endif
#endif

/*
 * 屏幕：默认总关（SCREEN_LOG=0）。打开后 BOOT/USB/FS/GUI 默认真；
 * SMP/MEM/NET/DRV/MISC 默认 quiet，避免 MADT/细日志刷满 bring-up 视口。
 */
#ifndef SCREEN_LOG
#define SCREEN_LOG 0
#endif

#if !SCREEN_LOG
#ifdef SCREEN_LOG_BOOT
#undef SCREEN_LOG_BOOT
#endif
#ifdef SCREEN_LOG_USB
#undef SCREEN_LOG_USB
#endif
#ifdef SCREEN_LOG_SMP
#undef SCREEN_LOG_SMP
#endif
#ifdef SCREEN_LOG_GUI
#undef SCREEN_LOG_GUI
#endif
#ifdef SCREEN_LOG_NET
#undef SCREEN_LOG_NET
#endif
#ifdef SCREEN_LOG_FS
#undef SCREEN_LOG_FS
#endif
#ifdef SCREEN_LOG_MEM
#undef SCREEN_LOG_MEM
#endif
#ifdef SCREEN_LOG_DRV
#undef SCREEN_LOG_DRV
#endif
#ifdef SCREEN_LOG_MISC
#undef SCREEN_LOG_MISC
#endif
#define SCREEN_LOG_BOOT 0
#define SCREEN_LOG_USB  0
#define SCREEN_LOG_SMP  0
#define SCREEN_LOG_GUI  0
#define SCREEN_LOG_NET  0
#define SCREEN_LOG_FS   0
#define SCREEN_LOG_MEM  0
#define SCREEN_LOG_DRV  0
#define SCREEN_LOG_MISC 0
#else
#ifndef SCREEN_LOG_BOOT
#define SCREEN_LOG_BOOT 1
#endif
#ifndef SCREEN_LOG_USB
#define SCREEN_LOG_USB 1
#endif
#ifndef SCREEN_LOG_SMP
#define SCREEN_LOG_SMP 0
#endif
#ifndef SCREEN_LOG_GUI
#define SCREEN_LOG_GUI 1
#endif
#ifndef SCREEN_LOG_NET
#define SCREEN_LOG_NET 0
#endif
#ifndef SCREEN_LOG_FS
#define SCREEN_LOG_FS 1
#endif
#ifndef SCREEN_LOG_MEM
#define SCREEN_LOG_MEM 0
#endif
#ifndef SCREEN_LOG_DRV
#define SCREEN_LOG_DRV 0
#endif
#ifndef SCREEN_LOG_MISC
#define SCREEN_LOG_MISC 0
#endif
#endif

/* 通道 ID（与上表对应；供 HalSerialWriteChannel） */
#define SLOG_BOOT 0
#define SLOG_USB  1
#define SLOG_SMP  2
#define SLOG_GUI  3
#define SLOG_NET  4
#define SLOG_FS   5
#define SLOG_MEM  6
#define SLOG_DRV  7
#define SLOG_MISC 8
#define SLOG_COUNT 9

#endif
