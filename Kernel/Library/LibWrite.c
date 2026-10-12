/*
 * LibWrite.c — Library 文本输出槽（不依赖 Hal）
 *
 * 未注册时丢弃；Core/Serial 或 Console 启动时 LibWriteRegister。
 */
#include "LibWrite.h"

static void (*gLibWrite)(const char *Text);

void LibWriteRegister(void (*Write)(const char *Text)) {
    gLibWrite = Write;
}

void LibWrite(const char *Text) {
    if (!Text || !gLibWrite) {
        return;
    }
    gLibWrite(Text);
}
