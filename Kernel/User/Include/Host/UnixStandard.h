/*
 * Host/UnixStandard.h — sleep / cwd / brk（selfhost：文件/进程见 Blocks/File.h · Process.h）
 */
#ifndef UNISTD_H
#define UNISTD_H

#include <System/Types.h>
#include <Blocks/File.h>
#include <Blocks/Process.h>
#include <Blocks/Thread.h>

void *brk(void *addr);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
unsigned sleep(unsigned seconds);
int usleep(unsigned usec);
unsigned msleep(unsigned ms);
unsigned long clock_ms(void);

#endif
