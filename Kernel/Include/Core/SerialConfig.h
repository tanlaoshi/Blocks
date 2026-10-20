/*
 * SerialConfig.h — 串口 + 屏幕（boot GOP）编译期开关（PR-K-log-switch）
 *
 * 串口总开关 SERIAL_ENABLE (build: SERIAL=)：
 *   0 → 不 Probe/不 Init UART；SerialWrite/RX 全空；无任何串口 TX
 *   1 → 正常 Probe；各模块由 SERIAL_* 决定是否往 UART 打
 *
 * 屏幕总开关 SCREEN_LOG：
 *   0 → boot GOP 不上字（ring 仍收；Mute/Mirror API 仍在）
 *   1 → 各模块 SCREEN_LOG_* 上滚
 *
 * 落盘总开关 FILE_LOG：
 *   0 → 不写 BOOT.LOG
 *   1 → 挂卷后和桌面起来后各写一次（默认开，无串口也能留日志）
 *
 * 分模块（仅对应总开=1 时有效；0=该模块 quiet）：
 *   BOOT USB SMP GUI NET FS MEM DRV MISC
 *
 * 构建：
 *   ./build.sh SERIAL=0
 *   ./build.sh SERIAL_USB=0 SERIAL_SMP=0
 *   ./build.sh                       # 默认 SCREEN_LOG=0（boot GOP 不上字）
 *   ./build.sh SCREEN_LOG=1          # 打开屏上滚动；再按模块 SCREEN_LOG_*=1
 *   ./build.sh SCREEN_LOG=1 SCREEN_LOG_SMP=1
 *   ./build.sh FILE_LOG=0            # 不写 BOOT.LOG
 *   NO_COM1=1 仍可用，等价于 SERIAL=0（兼容旧课堂开关）
 *
 * 说明：SERIAL=0 只关 UART；SCREEN_LOG 默认 0（省 4K blit）；UART 仍可由 SERIAL 控制。
 * ring 始终收。运行时 Mute / Mirror 仍是真机安全阀。
 * Shell 回显走 HalSerialWrite（MISC）+ Console FB，不纳入 SCREEN_LOG（本柱不做）。
 */
#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#ifndef SERIAL_ENABLE
#define SERIAL_ENABLE 0
#endif

#if !SERIAL_ENABLE
#ifdef SERIAL_BOOT
#undef SERIAL_BOOT
#endif
#ifdef SERIAL_USB
#undef SERIAL_USB
#endif
#ifdef SERIAL_SMP
#undef SERIAL_SMP
#endif
#ifdef SERIAL_GUI
#undef SERIAL_GUI
#endif
#ifdef SERIAL_NET
#undef SERIAL_NET
#endif
#ifdef SERIAL_FS
#undef SERIAL_FS
#endif
#ifdef SERIAL_MEM
#undef SERIAL_MEM
#endif
#ifdef SERIAL_DRV
#undef SERIAL_DRV
#endif
#ifdef SERIAL_MISC
#undef SERIAL_MISC
#endif
#define SERIAL_BOOT 0
#define SERIAL_USB  0
#define SERIAL_SMP  0
#define SERIAL_GUI  0
#define SERIAL_NET  0
#define SERIAL_FS   0
#define SERIAL_MEM  0
#define SERIAL_DRV  0
#define SERIAL_MISC 0
#else
#ifndef SERIAL_BOOT
#define SERIAL_BOOT 1
#endif
#ifndef SERIAL_USB
#define SERIAL_USB 1
#endif
#ifndef SERIAL_SMP
#define SERIAL_SMP 1
#endif
#ifndef SERIAL_GUI
#define SERIAL_GUI 1
#endif
#ifndef SERIAL_NET
#define SERIAL_NET 1
#endif
#ifndef SERIAL_FS
#define SERIAL_FS 1
#endif
#ifndef SERIAL_MEM
#define SERIAL_MEM 1
#endif
#ifndef SERIAL_DRV
#define SERIAL_DRV 1
#endif
#ifndef SERIAL_MISC
#define SERIAL_MISC 1
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

/* 落盘。与串口、屏幕独立。无串口的单机默认开。 */
#ifndef FILE_LOG
#define FILE_LOG 1
#endif

#endif
