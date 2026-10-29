/*
 * ftarena.h — freetype-port 静态 arena 的统计/自检接口。
 *
 * 不是 FreeType 公共 API：只给 FreeTypeRaster.c（串口诊断）和启动自检用。
 * 实现见 ftsystem_blocks.c（首次适配 free-list）。
 */
#ifndef FTARENA_H
#define FTARENA_H

/*
 * 取 arena 计数（单位：字节）。
 *   Total : arena 总容量
 *   Used  : 当前已分配块扣除块头后的容量（含 16 字节对齐余量）
 *   Peak  : 本次开机以来 Used 的最大值（FT_Done_Memory 不清零，便于看峰值）
 *   Fail  : 分配/realloc 失败次数（含 size<=0 与非法请求）
 * 任一指针可为 0。
 */
void FtArenaStats(unsigned long *Total, unsigned long *Used,
                  unsigned long *Peak, unsigned long *Fail);

/* 累计 alloc/free 调用次数（用来证明回收确实在发生，而不是只增不减）。 */
void FtArenaOps(unsigned long *Allocs, unsigned long *Frees);

/*
 * 把 arena 恢复成「一整块空闲」：清 Used，但保留 Peak/Fail/计数。
 * 仅 FT_Done_Memory 收尾与自检用；正常分配路径不要调。
 */
void FtArenaReset(void);

/*
 * 全量自检：物理块链、PrevSize、空闲链表、字节统计互相一致返回 1，否则 0。
 * O(n)，只在自检里调；坏链计数见 FtArenaBadness。
 */
int FtArenaCheck(void);

/* 自检/防御路径发现的坏块、坏指针、双重释放次数。正常应为 0。 */
unsigned long FtArenaBadness(void);

#endif
