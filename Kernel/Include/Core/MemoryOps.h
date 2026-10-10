/*
 * MemoryOps.h — 可替换物理页分配政策（K53）
 *
 * 【初学者】
 * 框架（公开 Allocate/Free）经 MemoryOpsGet() 调 *Locked；
 * 默认实现 MemoryBitmapOps() 即现有位图。以后可换学生政策而不改调用方。
 */
#ifndef MEMORY_OPS_H
#define MEMORY_OPS_H

#include "BootTypes.h"

typedef struct {
    void (*Init)(void);
    void *(*AllocPagesLocked)(UINT32 Count);
    void (*FreePagesLocked)(void *Page, UINT32 Count);
    int (*RetainPageLocked)(void *Page);
    void (*ReleasePageLocked)(void *Page);
} MEMORY_OPS;

void MemoryOpsRegister(const MEMORY_OPS *Ops);
const MEMORY_OPS *MemoryOpsGet(void);
const MEMORY_OPS *MemoryBitmapOps(void);

#endif
