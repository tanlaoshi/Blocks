/*
 * PhysicalMemoryOps.c — MEMORY_OPS 注册（K53）
 *
 * 【初学者】
 * - Core/Memory：策略表指针仓储；默认政策在 PhysicalMemoryBitmap.c 注册。
 * - 入口：MemoryOpsRegister / MemoryOpsGet。
 * - 边界：不直接碰位图；Allocate 走 MemoryOpsGet()->AllocatePages。
 */
#include "MemoryOps.h"

static const MEMORY_OPS *gOps;

/*
 * MemoryOpsRegister — 安装物理页分配政策
 *
 * 做什么：保存 MEMORY_OPS 指针（通常 BitmapOps）。
 * 谁调用：PhysicalMemoryInitialize 末尾。
 */
void MemoryOpsRegister(const MEMORY_OPS *Ops) {
    gOps = Ops;
}

/*
 * MemoryOpsGet — 当前物理页政策
 *
 * 做什么：返回 gOps；未注册则 NULL。
 * 谁调用：PhysicalMemoryAllocatePage 等。
 */
const MEMORY_OPS *MemoryOpsGet(void) {
    return gOps;
}
