/*
 * Blocks/File.h — 文件 fd API（selfhost；非 ISO C 的 open/read…）
 */
#ifndef BLOCKS_FILE_H
#define BLOCKS_FILE_H

#include <sys/types.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

int FileOpen(const char *path);
ssize_t FileRead(int fd, void *buf, size_t count);
ssize_t FileWrite(int fd, const void *buf, size_t count);
int FileClose(int fd);
off_t FileSeek(int fd, off_t offset, int whence);

#endif
