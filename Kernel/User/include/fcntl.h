/*
 * fcntl.h — 打开标志（selfhost：用 FileOpen，无 open()）
 */
#ifndef FCNTL_H
#define FCNTL_H

#include <Blocks/File.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200

#endif
