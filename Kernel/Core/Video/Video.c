/*
 * Video.c — Video 模块表项：背缓冲 Present（Theme/Font 同目录）
 *
 * 【初学者】
 * - Core/Video：Hal 背缓冲 + Present；Theme/Font 由 Gui 等按需初始化。
 * - 入口：VideoInitialize（ModulesRunFull）。
 * - 边界：不直接写 GOP；像素经 HalVideo*。
 */
#include "Video.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "SerialConfig.h"

/*
 * VideoInitialize — Video 模块表入口
 *
 * 做什么：Hal 背缓冲 + 可选 Present；不清屏。
 * 谁调用：ModulesRunFull（Driver 之后）。
 * 返回：0。
 */
int VideoInitialize(void) {
    /* 不 ClearScreen：接 Boot 黑底 */
    HalVideoInitializeBackbuffer();
    if (HalVideoBackbufferEnabled()) {
        HalSerialWriteChannel(SLOG_GUI, "Video: backbuffer on\n");
        HalVideoPresent();
        HalSerialWriteChannel(SLOG_GUI, "Video: present ok\n");
    } else {
        HalSerialWriteChannel(SLOG_GUI, "Video: backbuffer skip\n");
    }
    return 0;
}
