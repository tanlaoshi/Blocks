#ifndef ELF_LOAD_H
#define ELF_LOAD_H

#include "BootTypes.h"

typedef struct {
    UINT64 Entry;
    UINT64 StackTop; /* 内核为其准备的栈顶 */
} ELF_IMAGE;

/* 装 ET_EXEC PT_LOAD 到 p_vaddr（须在恒等窗内）；成功 0 */
int ElfLoaderFromMemory(const void *Image, UINTN Size, ELF_IMAGE *Out);

#endif
