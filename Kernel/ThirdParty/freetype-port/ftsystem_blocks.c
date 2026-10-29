/*
 * freestanding FT_Memory：静态 arena + 首次适配 free-list（相邻块合并）。
 * 替代 src/base/ftsystem.c（并开启 DISABLE_STREAM_SUPPORT）。
 *
 * 布局：arena 按 16 字节对齐切成物理块链，每块头 16 字节：
 *   PrevSize  前一块物理块的总大小（含头），首块为 0
 *   Size      本块总大小（含头），bit0=已用；大小恒为 16 的倍数
 *   NextFree  空闲链后继（偏移），FT_NIL=无
 *   PrevFree  空闲链前驱（偏移），FT_NIL=无
 *
 * 分配：16 对齐，沿空闲链首次适配；块尾剩余 >= 32 字节就切出去。
 * 释放：按 PrevSize/Size 找物理前后邻，空闲则合并，再挂回空闲链。
 * realloc：先试原地缩/扩（后继空闲块可并），不行才新分配+拷贝+释放；
 * 失败时旧块保持有效（FT_Memory 契约）。
 *
 * 内核无 malloc：全部状态在这份 BSS 里，确定性、无锁（调用方
 * FreeTypeRaster.c 已用 gFtLock 串行化 FT 调用）。
 */
#include <ft2build.h>
#include FT_CONFIG_CONFIG_H
#include <freetype/internal/ftdebug.h>
#include <freetype/ftsystem.h>
#include <freetype/fterrors.h>
#include <freetype/fttypes.h>

#include "ftarena.h"

/* 过大进 ELF BSS，Boot AllocatePages@1MB 会 Not Found；256K 够单 Face+栅格 */
#define FT_ARENA_BYTES (256u * 1024u)
#define FT_ALIGN 16u
#define FT_HDR 16u
#define FT_MIN_BLOCK 32u /* 头 + 至少 16 字节净荷，再小切了没意义 */
#define FT_NIL 0xFFFFFFFFu
#define FT_USED 1u
#define FT_SIZE_MASK 0xFFFFFFF0u

typedef struct FtBlock {
  unsigned int PrevSize;
  unsigned int Size;
  unsigned int NextFree;
  unsigned int PrevFree;
} FtBlock;

static unsigned char gFtArena[FT_ARENA_BYTES];
static unsigned int gFtFreeHead = FT_NIL;
static unsigned long gFtUsed;   /* 已用净容量（扣块头、含对齐余量） */
static unsigned long gFtPeak;   /* 开机以来 Used 峰值 */
static unsigned long gFtFail;   /* 分配失败次数 */
static unsigned long gFtAllocs; /* alloc 调用次数 */
static unsigned long gFtFrees;  /* free 调用次数 */
static unsigned long gFtBad;    /* 坏指针/双重释放/坏链 */
static int gFtArenaReady;

static FtBlock *FtAt(unsigned int Off) {
  return (FtBlock *)(void *)(gFtArena + Off);
}

static unsigned int FtAlign(unsigned long N) {
  return (unsigned int)((N + (FT_ALIGN - 1ul)) & ~(unsigned long)(FT_ALIGN - 1ul));
}

void FtArenaReset(void) {
  FtBlock *B = FtAt(0);

  B->PrevSize = 0;
  B->Size = FT_ARENA_BYTES; /* bit0=0：空闲 */
  B->NextFree = FT_NIL;
  B->PrevFree = FT_NIL;
  gFtFreeHead = 0;
  gFtUsed = 0;
  gFtArenaReady = 1;
}

static void FtEnsure(void) {
  if (!gFtArenaReady) {
    FtArenaReset();
  }
}

/* 空闲链是 LIFO，不做地址排序：合并只依赖物理块头，插入/摘除 O(1)。 */
static void FtListInsert(unsigned int Off) {
  FtBlock *B = FtAt(Off);

  B->PrevFree = FT_NIL;
  B->NextFree = gFtFreeHead;
  if (gFtFreeHead != FT_NIL) {
    FtAt(gFtFreeHead)->PrevFree = Off;
  }
  gFtFreeHead = Off;
}

static void FtListRemove(unsigned int Off) {
  FtBlock *B = FtAt(Off);

  if (B->PrevFree != FT_NIL) {
    FtAt(B->PrevFree)->NextFree = B->NextFree;
  } else {
    gFtFreeHead = B->NextFree;
  }
  if (B->NextFree != FT_NIL) {
    FtAt(B->NextFree)->PrevFree = B->PrevFree;
  }
  B->NextFree = FT_NIL;
  B->PrevFree = FT_NIL;
}

static void FtAddUsed(unsigned long N) {
  gFtUsed += N;
  if (gFtUsed > gFtPeak) {
    gFtPeak = gFtUsed;
  }
}

/*
 * 已空闲的物理块 Off 入链：先并后继、再并前驱，最后回填后继的 PrevSize。
 * 调用前 Off 必须已标空闲且不在空闲链里。
 */
static void FtLinkFree(unsigned int Off) {
  FtBlock *B = FtAt(Off);
  unsigned int Next;
  unsigned int Tail;

  Next = Off + B->Size;
  if (Next < FT_ARENA_BYTES) {
    FtBlock *Nb = FtAt(Next);

    if ((Nb->Size & FT_USED) == 0u) {
      FtListRemove(Next);
      B->Size += Nb->Size;
    }
  }
  if (B->PrevSize != 0u) {
    unsigned int Prev = Off - B->PrevSize;
    FtBlock *Pb = FtAt(Prev);

    if ((Pb->Size & FT_USED) == 0u) {
      FtListRemove(Prev);
      Pb->Size += B->Size;
      B = Pb;
      Off = Prev;
    }
  }
  Tail = Off + B->Size;
  if (Tail < FT_ARENA_BYTES) {
    FtAt(Tail)->PrevSize = B->Size;
  }
  FtListInsert(Off);
}

/*
 * 已用块 Off 的容量从 OldCap（当前统计值）改成 Need（含头），多出部分转空闲。
 * 调用前 B->Size 必须已是「未切分前的总大小」，且 total-Need >= FT_MIN_BLOCK。
 */
static void FtTrimUsed(unsigned int Off, unsigned long OldCap, unsigned int Need) {
  FtBlock *B = FtAt(Off);
  unsigned int Total = B->Size & FT_SIZE_MASK;
  unsigned int Tail = Off + Need;
  FtBlock *Tb;

  gFtUsed = gFtUsed - OldCap + (Need - FT_HDR);
  if (gFtUsed > gFtPeak) {
    gFtPeak = gFtUsed;
  }
  B->Size = Need | FT_USED;
  Tb = FtAt(Tail);
  Tb->PrevSize = Need;
  Tb->Size = Total - Need;
  Tb->NextFree = FT_NIL;
  Tb->PrevFree = FT_NIL;
  FtLinkFree(Tail);
}

static void *FtAlloc(FT_Memory Memory, long Size) {
  unsigned long Payload;
  unsigned int Need;
  unsigned int Off;
  FtBlock *B;

  (void)Memory;
  FtEnsure();
  gFtAllocs++;
  if (Size <= 0) {
    gFtFail++;
    return 0;
  }
  Payload = (unsigned long)Size;
  /* 先按 64 位判上限，避免 Payload+头 回绕后被 FtAlign 截成小尺寸 */
  if (Payload > FT_ARENA_BYTES - FT_HDR) {
    gFtFail++;
    return 0;
  }
  Need = FtAlign(Payload + FT_HDR);
  if (Need < FT_MIN_BLOCK) {
    Need = FT_MIN_BLOCK;
  }
  Off = gFtFreeHead;
  while (Off != FT_NIL) {
    B = FtAt(Off);
    if ((B->Size & FT_SIZE_MASK) >= Need) {
      break;
    }
    Off = B->NextFree;
  }
  if (Off == FT_NIL) {
    gFtFail++;
    return 0;
  }
  B = FtAt(Off);
  FtListRemove(Off);
  if ((B->Size & FT_SIZE_MASK) - Need >= FT_MIN_BLOCK) {
    unsigned int Rest = Off + Need;
    unsigned int Total = B->Size & FT_SIZE_MASK;
    unsigned int After = Off + Total;
    FtBlock *Rb = FtAt(Rest);

    Rb->PrevSize = Need;
    Rb->Size = Total - Need;
    B->Size = Need;
    if (After < FT_ARENA_BYTES) {
      FtAt(After)->PrevSize = Rb->Size;
    }
    FtListInsert(Rest);
  }
  B->Size = (B->Size & FT_SIZE_MASK) | FT_USED;
  FtAddUsed((B->Size & FT_SIZE_MASK) - FT_HDR);
  return (void *)(gFtArena + Off + FT_HDR);
}

static void FtFree(FT_Memory Memory, void *Block) {
  unsigned int Off;
  FtBlock *B;
  unsigned long Cap;

  (void)Memory;
  if (!Block) {
    return;
  }
  FtEnsure();
  gFtFrees++;
  Off = (unsigned int)((unsigned char *)Block - gFtArena);
  if (Off < FT_HDR || Off >= FT_ARENA_BYTES ||
      (Off & (FT_ALIGN - 1u)) != 0u) {
    gFtBad++;
    return;
  }
  Off -= FT_HDR;
  B = FtAt(Off);
  Cap = B->Size & FT_SIZE_MASK;
  if ((B->Size & FT_USED) == 0u || Cap < FT_MIN_BLOCK ||
      Off + Cap > FT_ARENA_BYTES) {
    gFtBad++;
    return;
  }
  if (Cap - FT_HDR > gFtUsed) {
    /* 记账对不上：宁可少减也不要把 Used 减下溢，并记为坏块。 */
    gFtBad++;
    gFtUsed = 0;
  } else {
    gFtUsed -= Cap - FT_HDR;
  }
  B->Size &= FT_SIZE_MASK;
  B->NextFree = FT_NIL;
  B->PrevFree = FT_NIL;
  FtLinkFree(Off);
}

static void *FtRealloc(FT_Memory Memory, long CurSize, long NewSize,
                       void *Block) {
  unsigned int Off;
  unsigned int Need;
  unsigned int Total;
  unsigned long Cap;
  FtBlock *B;
  void *P;
  unsigned long N;
  unsigned long I;

  (void)CurSize;
  if (!Block) {
    return FtAlloc(Memory, NewSize);
  }
  if (NewSize <= 0) {
    FtFree(Memory, Block);
    return 0;
  }
  FtEnsure();
  Off = (unsigned int)((unsigned char *)Block - gFtArena);
  if (Off < FT_HDR || Off >= FT_ARENA_BYTES ||
      (Off & (FT_ALIGN - 1u)) != 0u) {
    gFtBad++;
    return 0;
  }
  Off -= FT_HDR;
  B = FtAt(Off);
  if ((B->Size & FT_USED) == 0u) {
    gFtBad++;
    return 0;
  }
  Total = B->Size & FT_SIZE_MASK;
  Cap = Total - FT_HDR;
  /* arena 装不下的请求直接失败（旧块保持有效），也别让 Need 截断 */
  if ((unsigned long)NewSize > FT_ARENA_BYTES - FT_HDR) {
    gFtFail++;
    return 0;
  }
  Need = FtAlign((unsigned long)NewSize + FT_HDR);
  if (Need < FT_MIN_BLOCK) {
    Need = FT_MIN_BLOCK;
  }
  if ((unsigned long)NewSize <= Cap) {
    if (Total >= Need && Total - Need >= FT_MIN_BLOCK) {
      FtTrimUsed(Off, Cap, Need);
    }
    return Block;
  }
  /* 原地扩：后继是空闲块且并起来够大 */
  {
    unsigned int Tail = Off + Total;

    if (Tail < FT_ARENA_BYTES) {
      FtBlock *Nb = FtAt(Tail);

      if ((Nb->Size & FT_USED) == 0u) {
        unsigned int Merged = Total + (Nb->Size & FT_SIZE_MASK);

        if (Merged >= Need) {
          FtListRemove(Tail);
          B->Size = Merged | FT_USED;
          if (Merged - Need >= FT_MIN_BLOCK) {
            FtTrimUsed(Off, Cap, Need);
          } else {
            unsigned int After = Off + Merged;

            FtAddUsed(Merged - Total);
            if (After < FT_ARENA_BYTES) {
              FtAt(After)->PrevSize = Merged;
            }
          }
          return Block;
        }
      }
    }
  }
  P = FtAlloc(Memory, NewSize);
  if (!P) {
    /* 契约：失败时旧块保持有效 */
    return 0;
  }
  N = (unsigned long)NewSize;
  if (Cap < N) {
    N = Cap;
  }
  {
    unsigned char *D = (unsigned char *)P;
    unsigned char *S = (unsigned char *)Block;

    for (I = 0; I < N; I++) {
      D[I] = S[I];
    }
  }
  FtFree(Memory, Block);
  return P;
}

void FtArenaStats(unsigned long *Total, unsigned long *Used,
                  unsigned long *Peak, unsigned long *Fail) {
  FtEnsure();
  if (Total) {
    *Total = FT_ARENA_BYTES;
  }
  if (Used) {
    *Used = gFtUsed;
  }
  if (Peak) {
    *Peak = gFtPeak;
  }
  if (Fail) {
    *Fail = gFtFail;
  }
}

void FtArenaOps(unsigned long *Allocs, unsigned long *Frees) {
  if (Allocs) {
    *Allocs = gFtAllocs;
  }
  if (Frees) {
    *Frees = gFtFrees;
  }
}

unsigned long FtArenaBadness(void) {
  return gFtBad;
}

int FtArenaCheck(void) {
  unsigned int Off;
  unsigned int Prev;
  unsigned long Used;
  unsigned int FreePhys;
  unsigned int FreeList;
  unsigned long MaxBlocks;

  FtEnsure();
  MaxBlocks = FT_ARENA_BYTES / FT_MIN_BLOCK + 1ul;
  Off = 0;
  Prev = 0;
  Used = 0;
  FreePhys = 0;
  while (Off < FT_ARENA_BYTES) {
    FtBlock *B = FtAt(Off);
    unsigned int Size = B->Size & FT_SIZE_MASK;

    if (Size < FT_MIN_BLOCK || (Size & (FT_ALIGN - 1u)) != 0u ||
        Off + Size > FT_ARENA_BYTES || B->PrevSize != Prev) {
      return 0;
    }
    if (B->Size & FT_USED) {
      Used += Size - FT_HDR;
    } else {
      FreePhys++;
    }
    Prev = Size;
    Off += Size;
  }
  if (Off != FT_ARENA_BYTES || Used != gFtUsed) {
    return 0;
  }
  Off = gFtFreeHead;
  FreeList = 0;
  while (Off != FT_NIL) {
    FtBlock *B;
    unsigned int Size;

    if (FreeList > MaxBlocks || Off >= FT_ARENA_BYTES ||
        (Off & (FT_ALIGN - 1u)) != 0u) {
      return 0;
    }
    B = FtAt(Off);
    Size = B->Size & FT_SIZE_MASK;
    if (Size < FT_MIN_BLOCK || (B->Size & FT_USED) != 0u) {
      return 0;
    }
    if (B->PrevFree != FT_NIL) {
      if (B->PrevFree >= FT_ARENA_BYTES ||
          FtAt(B->PrevFree)->NextFree != Off) {
        return 0;
      }
    } else if (gFtFreeHead != Off) {
      return 0;
    }
    if (B->NextFree != FT_NIL) {
      if (B->NextFree >= FT_ARENA_BYTES ||
          FtAt(B->NextFree)->PrevFree != Off) {
        return 0;
      }
    }
    FreeList++;
    Off = B->NextFree;
  }
  if (FreeList != FreePhys) {
    return 0;
  }
  /* 相邻空闲块必须已合并；否则说明释放路径漏合并（碎片） */
  Off = 0;
  while (Off < FT_ARENA_BYTES) {
    FtBlock *B = FtAt(Off);
    unsigned int Size = B->Size & FT_SIZE_MASK;

    if ((B->Size & FT_USED) == 0u && Off + Size < FT_ARENA_BYTES &&
        (FtAt(Off + Size)->Size & FT_USED) == 0u) {
      return 0;
    }
    Off += Size;
  }
  return 1;
}

FT_BASE_DEF(FT_Memory)
FT_New_Memory(void) {
  FT_Memory Memory;

  FtEnsure();
  Memory = (FT_Memory)FtAlloc(0, (long)sizeof(*Memory));
  if (!Memory) {
    return 0;
  }
  Memory->user = 0;
  Memory->alloc = FtAlloc;
  Memory->realloc = FtRealloc;
  Memory->free = FtFree;
  return Memory;
}

FT_BASE_DEF(void)
FT_Done_Memory(FT_Memory Memory) {
  if (Memory) {
    FtFree(0, Memory);
  }
  /*
   * 正常路径 FT 对象已全部释放，Used==0；异常路径若还有残留，直接整块归零，
   * 免得下次 FT_Init 在旧块上重建（bump 版这里是 gFtUsed=0，语义一致）。
   */
  if (gFtUsed != 0) {
    FtArenaReset();
  }
}
