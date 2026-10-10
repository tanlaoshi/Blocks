/*
 * PhysicalMemoryOps.c — MEMORY_OPS 注册（K53）
 */
#include "MemoryOps.h"

static const MEMORY_OPS *gOps;

void MemoryOpsRegister(const MEMORY_OPS *Ops) {
    gOps = Ops;
}

const MEMORY_OPS *MemoryOpsGet(void) {
    return gOps;
}
