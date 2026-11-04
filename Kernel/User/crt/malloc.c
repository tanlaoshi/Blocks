/*
 * malloc.c — brk bump + 教学 freelist（展览级命名）
 *
 * free 挂回链表；malloc 先 first-fit 复用。不合并邻块、不向 brk 收缩。
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct MallocHeader {
    size_t Capacity;                 /* 对齐后的用户可用字节 */
    size_t UserSize;                 /* 最近一次请求大小 */
    struct MallocHeader *NextFree; /* 仅空闲块有效 */
} MallocHeader;

static MallocHeader *FreeListHead;

static size_t AlignUp(size_t Value, size_t Alignment) {
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

static MallocHeader *TakeFromFreeList(size_t NeedCapacity) {
    MallocHeader *Previous = 0;
    MallocHeader *Cursor = FreeListHead;

    while (Cursor) {
        if (Cursor->Capacity >= NeedCapacity) {
            if (Previous) {
                Previous->NextFree = Cursor->NextFree;
            } else {
                FreeListHead = Cursor->NextFree;
            }
            Cursor->NextFree = 0;
            return Cursor;
        }
        Previous = Cursor;
        Cursor = Cursor->NextFree;
    }
    return 0;
}

static MallocHeader *AllocateFromBreak(size_t NeedCapacity) {
    size_t Total;
    void *OldBreak;
    void *NewBreak;
    MallocHeader *Header;

    Total = sizeof(MallocHeader) + NeedCapacity;
    OldBreak = brk((void *)0);
    if (OldBreak == (void *)(long)-1) {
        return 0;
    }
    NewBreak = (void *)((char *)OldBreak + Total);
    if (brk(NewBreak) != NewBreak) {
        return 0;
    }
    Header = (MallocHeader *)OldBreak;
    Header->Capacity = NeedCapacity;
    Header->NextFree = 0;
    return Header;
}

void *malloc(size_t ByteCount) {
    size_t NeedCapacity;
    MallocHeader *Header;

    if (ByteCount == 0) {
        return 0;
    }
    NeedCapacity = AlignUp(ByteCount, 8);
    Header = TakeFromFreeList(NeedCapacity);
    if (!Header) {
        Header = AllocateFromBreak(NeedCapacity);
        if (!Header) {
            return 0;
        }
    }
    Header->UserSize = ByteCount;
    return Header + 1;
}

void *calloc(size_t ElementCount, size_t ElementSize) {
    size_t ByteCount;
    void *Block;

    if (ElementCount == 0 || ElementSize == 0) {
        return 0;
    }
    if (ElementSize != 0 && ElementCount > ((size_t)-1) / ElementSize) {
        return 0;
    }
    ByteCount = ElementCount * ElementSize;
    Block = malloc(ByteCount);
    if (Block) {
        memset(Block, 0, ByteCount);
    }
    return Block;
}

void *realloc(void *Block, size_t ByteCount) {
    MallocHeader *Header;
    void *NewBlock;
    size_t CopyCount;

    if (!Block) {
        return malloc(ByteCount);
    }
    if (ByteCount == 0) {
        free(Block);
        return 0;
    }
    Header = (MallocHeader *)Block - 1;
    if (ByteCount <= Header->Capacity) {
        Header->UserSize = ByteCount;
        return Block;
    }
    NewBlock = malloc(ByteCount);
    if (!NewBlock) {
        return 0;
    }
    CopyCount = Header->UserSize;
    if (CopyCount > ByteCount) {
        CopyCount = ByteCount;
    }
    memcpy(NewBlock, Block, CopyCount);
    free(Block);
    return NewBlock;
}

void free(void *Block) {
    MallocHeader *Header;

    if (!Block) {
        return;
    }
    Header = (MallocHeader *)Block - 1;
    Header->UserSize = 0;
    Header->NextFree = FreeListHead;
    FreeListHead = Header;
}
