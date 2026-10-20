/*
 * DriverBlock.h — Block 类适配（PR-D2）
 *
 * 驱动 Bind 时调用 DriverBlockAttach；FAT/VFS 仍只见 Block*。
 */
#ifndef DRIVER_BLOCK_H
#define DRIVER_BLOCK_H

#include "Block.h"

/* 类适配：挂上 Block 后端（内部 BlockRegisterBackend） */
int DriverBlockAttach(const BLOCK_BACKEND *Backend);

#endif
