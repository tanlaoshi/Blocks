/*
 * ToySerialConfig.h — 串口 / 屏上日志的编译期开关
 *
 * 【初学者 · 为什么要这么多开关？】
 * 开机日志若全开，串口和屏幕会被 USB/SMP/MEM 细节刷爆，反而不好看清主线。
 * 所以分两层：
 *   TOY_SERIAL      — 总闸：0=完全不碰 UART
 *   TOY_SERIAL_BOOT/USB/… — 分模块：总闸开着时，某模块仍可 quiet
 * 屏幕同理：TOY_SCREEN_LOG 与 TOY_SCREEN_LOG_*（屏上滚动日志用）。
 *
 * 构建：
 *   ./build.sh x64              # 默认 TOY_SERIAL=1
 *   ./build.sh x64 SERIAL=0     # 关掉 UART
 *
 * 通道号 TOY_SLOG_* 给 HalSerialWriteChannel 用。
 */
#ifndef TOY_SERIAL_CONFIG_H
#define TOY_SERIAL_CONFIG_H

#ifndef TOY_SERIAL
#define TOY_SERIAL 0
#endif

#if !TOY_SERIAL
#ifdef TOY_SERIAL_BOOT
#undef TOY_SERIAL_BOOT
#endif
#ifdef TOY_SERIAL_USB
#undef TOY_SERIAL_USB
#endif
#ifdef TOY_SERIAL_SMP
#undef TOY_SERIAL_SMP
#endif
#ifdef TOY_SERIAL_GUI
#undef TOY_SERIAL_GUI
#endif
#ifdef TOY_SERIAL_NET
#undef TOY_SERIAL_NET
#endif
#ifdef TOY_SERIAL_FS
#undef TOY_SERIAL_FS
#endif
#ifdef TOY_SERIAL_MEM
#undef TOY_SERIAL_MEM
#endif
#ifdef TOY_SERIAL_DRV
#undef TOY_SERIAL_DRV
#endif
#ifdef TOY_SERIAL_MISC
#undef TOY_SERIAL_MISC
#endif
#define TOY_SERIAL_BOOT 0
#define TOY_SERIAL_USB  0
#define TOY_SERIAL_SMP  0
#define TOY_SERIAL_GUI  0
#define TOY_SERIAL_NET  0
#define TOY_SERIAL_FS   0
#define TOY_SERIAL_MEM  0
#define TOY_SERIAL_DRV  0
#define TOY_SERIAL_MISC 0
#else
#ifndef TOY_SERIAL_BOOT
#define TOY_SERIAL_BOOT 1
#endif
#ifndef TOY_SERIAL_USB
#define TOY_SERIAL_USB 1
#endif
#ifndef TOY_SERIAL_SMP
#define TOY_SERIAL_SMP 1
#endif
#ifndef TOY_SERIAL_GUI
#define TOY_SERIAL_GUI 1
#endif
#ifndef TOY_SERIAL_NET
#define TOY_SERIAL_NET 1
#endif
#ifndef TOY_SERIAL_FS
#define TOY_SERIAL_FS 1
#endif
#ifndef TOY_SERIAL_MEM
#define TOY_SERIAL_MEM 1
#endif
#ifndef TOY_SERIAL_DRV
#define TOY_SERIAL_DRV 1
#endif
#ifndef TOY_SERIAL_MISC
#define TOY_SERIAL_MISC 1
#endif
#endif

/*
 * 屏幕：默认总关（SCREEN_LOG=0）。打开后 BOOT/USB/FS/GUI 默认真；
 * SMP/MEM/NET/DRV/MISC 默认 quiet，避免 MADT/细日志刷满 bring-up 视口。
 */
#ifndef TOY_SCREEN_LOG
#define TOY_SCREEN_LOG 0
#endif

#if !TOY_SCREEN_LOG
#ifdef TOY_SCREEN_LOG_BOOT
#undef TOY_SCREEN_LOG_BOOT
#endif
#ifdef TOY_SCREEN_LOG_USB
#undef TOY_SCREEN_LOG_USB
#endif
#ifdef TOY_SCREEN_LOG_SMP
#undef TOY_SCREEN_LOG_SMP
#endif
#ifdef TOY_SCREEN_LOG_GUI
#undef TOY_SCREEN_LOG_GUI
#endif
#ifdef TOY_SCREEN_LOG_NET
#undef TOY_SCREEN_LOG_NET
#endif
#ifdef TOY_SCREEN_LOG_FS
#undef TOY_SCREEN_LOG_FS
#endif
#ifdef TOY_SCREEN_LOG_MEM
#undef TOY_SCREEN_LOG_MEM
#endif
#ifdef TOY_SCREEN_LOG_DRV
#undef TOY_SCREEN_LOG_DRV
#endif
#ifdef TOY_SCREEN_LOG_MISC
#undef TOY_SCREEN_LOG_MISC
#endif
#define TOY_SCREEN_LOG_BOOT 0
#define TOY_SCREEN_LOG_USB  0
#define TOY_SCREEN_LOG_SMP  0
#define TOY_SCREEN_LOG_GUI  0
#define TOY_SCREEN_LOG_NET  0
#define TOY_SCREEN_LOG_FS   0
#define TOY_SCREEN_LOG_MEM  0
#define TOY_SCREEN_LOG_DRV  0
#define TOY_SCREEN_LOG_MISC 0
#else
#ifndef TOY_SCREEN_LOG_BOOT
#define TOY_SCREEN_LOG_BOOT 1
#endif
#ifndef TOY_SCREEN_LOG_USB
#define TOY_SCREEN_LOG_USB 1
#endif
#ifndef TOY_SCREEN_LOG_SMP
#define TOY_SCREEN_LOG_SMP 0
#endif
#ifndef TOY_SCREEN_LOG_GUI
#define TOY_SCREEN_LOG_GUI 1
#endif
#ifndef TOY_SCREEN_LOG_NET
#define TOY_SCREEN_LOG_NET 0
#endif
#ifndef TOY_SCREEN_LOG_FS
#define TOY_SCREEN_LOG_FS 1
#endif
#ifndef TOY_SCREEN_LOG_MEM
#define TOY_SCREEN_LOG_MEM 0
#endif
#ifndef TOY_SCREEN_LOG_DRV
#define TOY_SCREEN_LOG_DRV 0
#endif
#ifndef TOY_SCREEN_LOG_MISC
#define TOY_SCREEN_LOG_MISC 0
#endif
#endif

/* 通道 ID（与上表对应；供 HalSerialWriteChannel） */
#define TOY_SLOG_BOOT 0
#define TOY_SLOG_USB  1
#define TOY_SLOG_SMP  2
#define TOY_SLOG_GUI  3
#define TOY_SLOG_NET  4
#define TOY_SLOG_FS   5
#define TOY_SLOG_MEM  6
#define TOY_SLOG_DRV  7
#define TOY_SLOG_MISC 8
#define TOY_SLOG_COUNT 9

#endif
