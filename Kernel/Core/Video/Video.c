/*
 * Video.c — Video 模块表项：背缓冲 Present（Theme/Font 同目录）
 */
#include "Video.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "ToySerialConfig.h"

int VideoInitialize(void) {
    /* 不 ClearScreen：接 Boot 黑底 */
    HalVideoInitializeBackbuffer();
    if (HalVideoBackbufferEnabled()) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Video: backbuffer on\n");
        HalVideoPresent();
        HalSerialWriteChannel(TOY_SLOG_GUI, "Video: present ok\n");
    } else {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Video: backbuffer skip\n");
    }
    return 0;
}
