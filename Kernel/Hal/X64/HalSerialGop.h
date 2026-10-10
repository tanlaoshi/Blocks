/*
 * HalSerialGop.h — SCREEN_LOG=1 时把串口日志镜像到帧缓冲
 *
 * 【初学者】
 * - Hal/X64：HalSerialWriteChannel 可选经本模块滚屏。
 * - 入口：HalSerialGopEnable / HalSerialGopWrite。
 */
#ifndef HAL_SERIAL_GOP_H
#define HAL_SERIAL_GOP_H

void HalSerialGopTryMirror(int Channel, const char *Text);

#endif
