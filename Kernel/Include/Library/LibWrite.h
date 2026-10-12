/*
 * LibWrite.h — Library 文本输出槽（不 #include Console / Hal）
 *
 * Serial 或 ConsoleInitialize 注册写函数；未注册则 LibWrite 空操作。
 */
#ifndef LIB_WRITE_H
#define LIB_WRITE_H

void LibWriteRegister(void (*Write)(const char *Text));
void LibWrite(const char *Text);

#endif
