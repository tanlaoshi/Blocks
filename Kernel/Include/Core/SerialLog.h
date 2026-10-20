/*
 * SerialLog.h — 分模块串口/屏幕日志宏
 *
 * Log*     ：始终进 ring；UART ← SERIAL_*；屏 ← SCREEN_LOG_* + Mirror/Mute
 * BootMark*：同上（里程碑常用 BOOT/USB；默认 SCREEN_LOG_USB=1 上屏）
 *
 * SERIAL_ENABLE=0：无 UART；ring / 屏仍可由 SCREEN_LOG 控制
 * SCREEN_LOG=0：boot GOP 不上字；UART 仍可由 SERIAL 控制
 */
#ifndef SERIAL_LOG_H
#define SERIAL_LOG_H

#include "SerialConfig.h"
#include "HalSerial.h"

#define BootMarkBoot(Text)  HalSerialBootMarkChannel(SLOG_BOOT, (Text))
#define BootMarkUsb(Text)   HalSerialBootMarkChannel(SLOG_USB, (Text))
#define BootMarkSmp(Text)   HalSerialBootMarkChannel(SLOG_SMP, (Text))

#define LogBoot(Text)       HalSerialWriteChannel(SLOG_BOOT, (Text))
#define LogUsb(Text)        HalSerialWriteChannel(SLOG_USB, (Text))
#define LogSmp(Text)        HalSerialWriteChannel(SLOG_SMP, (Text))
#define LogGui(Text)        HalSerialWriteChannel(SLOG_GUI, (Text))
#define LogNet(Text)        HalSerialWriteChannel(SLOG_NET, (Text))
#define LogFs(Text)         HalSerialWriteChannel(SLOG_FS, (Text))
#define LogMem(Text)        HalSerialWriteChannel(SLOG_MEM, (Text))
#define LogDrv(Text)        HalSerialWriteChannel(SLOG_DRV, (Text))
#define LogMisc(Text)       HalSerialWriteChannel(SLOG_MISC, (Text))

#define LogBootHex32(V)     HalSerialWriteChannelHex32(SLOG_BOOT, (V))
#define LogUsbHex32(V)      HalSerialWriteChannelHex32(SLOG_USB, (V))
#define LogSmpHex32(V)      HalSerialWriteChannelHex32(SLOG_SMP, (V))
#define LogSmpHex64(V)      HalSerialWriteChannelHex64(SLOG_SMP, (V))
#define LogNetHex32(V)      HalSerialWriteChannelHex32(SLOG_NET, (V))
#define LogFsHex32(V)       HalSerialWriteChannelHex32(SLOG_FS, (V))
#define LogMemHex32(V)      HalSerialWriteChannelHex32(SLOG_MEM, (V))
#define LogDrvHex32(V)      HalSerialWriteChannelHex32(SLOG_DRV, (V))
#define LogMiscHex32(V)     HalSerialWriteChannelHex32(SLOG_MISC, (V))
#define LogMiscHex64(V)     HalSerialWriteChannelHex64(SLOG_MISC, (V))

#endif
