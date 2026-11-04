/*
 * malloc.c — 经 SYS_MEMORY_BRK 的 bump 堆（展览级命名）
 *
 * 块头记下用户请求大小，供 realloc 拷贝；free 教学省略回收。
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    size_t UserSize;
} MallocHeader;

void *malloc(size_t ByteCount) {
    size_t Alignment = 8;
    size_t Need;
    void *OldBreak;
    void *NewBreak;
    MallocHeader *Header;

    if (ByteCount == 0) {
        return 0;
    }
    Need = sizeof(MallocHeader) +
           ((ByteCount + Alignment - 1) & ~(Alignment - 1));
    OldBreak = brk((void *)0);
    if (OldBreak == (void *)(long)-1) {
        return 0;
    }
    NewBreak = (void *)((char *)OldBreak + Need);
    if (brk(NewBreak) != NewBreak) {
        return 0;
    }
    Header = (MallocHeader *)OldBreak;
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
    size_t OldSize;

    if (!Block) {
        return malloc(ByteCount);
    }
    if (ByteCount == 0) {
        free(Block);
        return 0;
    }
    Header = (MallocHeader *)Block - 1;
    OldSize = Header->UserSize;
    if (ByteCount <= OldSize) {
        Header->UserSize = ByteCount;
        return Block;
    }
    NewBlock = malloc(ByteCount);
    if (!NewBlock) {
        return 0;
    }
    memcpy(NewBlock, Block, OldSize);
    free(Block);
    return NewBlock;
}

void free(void *Block) {
    (void)Block;
    /* bump 堆：CRT 不回收；可 brk 收缩但教学省略 */
}
