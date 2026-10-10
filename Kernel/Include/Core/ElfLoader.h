#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "BootTypes.h"
#include "VirtualMemory.h"

typedef struct {
    UINT64 Entry;
    UINT64 StackTop; /* 栈顶虚址 */
} ELF_IMAGE;

/* 装 ET_EXEC PT_LOAD 到 p_vaddr（须在恒等窗内）；成功 0 */
int ElfLoaderFromMemory(const void *Image, UINTN Size, ELF_IMAGE *Out);

/*
 * K49：装进独立 AddressSpace（私有物理页 + MapRange），不写内核恒等窗里的旧页。
 * Space 非 NULL；成功 0。
 */
int ElfLoaderFromMemoryToSpace(const void *Image, UINTN Size,
                               VIRTUAL_ADDRESS_SPACE *Space, ELF_IMAGE *Out);

#endif
