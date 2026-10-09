#ifndef FAT_FILE_H
#define FAT_FILE_H

#include "BootTypes.h"

/*
 * 从已 Init 的块设备根目录读 8.3 文件到 Buf。
 * Name83：11 字节（如 "HELLO   ELF"）。
 * 成功返回字节数；失败 -1。*OutSize 可选。
 */
int FatFileRead83(const char Name83[11], void *Buf, UINT32 Cap, UINT32 *OutSize);

#endif
