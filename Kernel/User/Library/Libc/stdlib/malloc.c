/*
 * malloc.c — brk bump + 教学 freelist（全称下划线）
 *
 * free 挂回链表；malloc 先 first-fit 复用。不合并邻块、不向 brk 收缩。
 */
#include <stdlib.h>
#include <string.h>
#include <Host/UnixStandard.h>

typedef struct malloc_header {
    size_t capacity;                 /* 对齐后的用户可用字节 */
    size_t user_size;                 /* 最近一次请求大小 */
    struct malloc_header *next_free; /* 仅空闲块有效 */
} malloc_header;

static malloc_header *free_list_head;

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static malloc_header *take_from_free_list(size_t need_capacity) {
    malloc_header *previous = 0;
    malloc_header *cursor = free_list_head;

    while (cursor) {
        if (cursor->capacity >= need_capacity) {
            if (previous) {
                previous->next_free = cursor->next_free;
            } else {
                free_list_head = cursor->next_free;
            }
            cursor->next_free = 0;
            return cursor;
        }
        previous = cursor;
        cursor = cursor->next_free;
    }
    return 0;
}

static malloc_header *allocate_from_break(size_t need_capacity) {
    size_t total;
    void *old_break;
    void *new_break;
    malloc_header *header;

    total = sizeof(malloc_header) + need_capacity;
    old_break = brk((void *)0);
    if (old_break == (void *)(long)-1) {
        return 0;
    }
    new_break = (void *)((char *)old_break + total);
    if (brk(new_break) != new_break) {
        return 0;
    }
    header = (malloc_header *)old_break;
    header->capacity = need_capacity;
    header->next_free = 0;
    return header;
}

void *malloc(size_t byte_count) {
    size_t need_capacity;
    malloc_header *header;

    if (byte_count == 0) {
        return 0;
    }
    need_capacity = align_up(byte_count, 8);
    header = take_from_free_list(need_capacity);
    if (!header) {
        header = allocate_from_break(need_capacity);
        if (!header) {
            return 0;
        }
    }
    header->user_size = byte_count;
    return header + 1;
}

void *calloc(size_t element_count, size_t element_size) {
    size_t byte_count;
    void *block;

    if (element_count == 0 || element_size == 0) {
        return 0;
    }
    if (element_size != 0 && element_count > ((size_t)-1) / element_size) {
        return 0;
    }
    byte_count = element_count * element_size;
    block = malloc(byte_count);
    if (block) {
        memset(block, 0, byte_count);
    }
    return block;
}

void *realloc(void *block, size_t byte_count) {
    malloc_header *header;
    void *new_block;
    size_t copy_count;

    if (!block) {
        return malloc(byte_count);
    }
    if (byte_count == 0) {
        free(block);
        return 0;
    }
    header = (malloc_header *)block - 1;
    if (byte_count <= header->capacity) {
        header->user_size = byte_count;
        return block;
    }
    new_block = malloc(byte_count);
    if (!new_block) {
        return 0;
    }
    copy_count = header->user_size;
    if (copy_count > byte_count) {
        copy_count = byte_count;
    }
    memcpy(new_block, block, copy_count);
    free(block);
    return new_block;
}

void free(void *block) {
    malloc_header *header;

    if (!block) {
        return;
    }
    header = (malloc_header *)block - 1;
    header->user_size = 0;
    header->next_free = free_list_head;
    free_list_head = header;
}
