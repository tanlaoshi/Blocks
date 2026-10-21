/*
 * freestanding FT_Memory：静态 arena，无 malloc/FILE。
 * 替代 src/base/ftsystem.c（并开启 DISABLE_STREAM_SUPPORT）。
 */
#include <ft2build.h>
#include FT_CONFIG_CONFIG_H
#include <freetype/internal/ftdebug.h>
#include <freetype/ftsystem.h>
#include <freetype/fterrors.h>
#include <freetype/fttypes.h>

/* 过大进 ELF BSS，Boot AllocatePages@1MB 会 Not Found；256K 够单 Face+栅格 */
#define FT_ARENA_BYTES (256u * 1024u)

static unsigned char gFtArena[FT_ARENA_BYTES];
static unsigned long gFtUsed;

static void *FtAlloc(FT_Memory memory, long size) {
  unsigned long need;
  void *p;

  (void)memory;
  if (size <= 0) {
    return 0;
  }
  need = ((unsigned long)size + 15ul) & ~15ul;
  if (gFtUsed + need > FT_ARENA_BYTES) {
    return 0;
  }
  p = gFtArena + gFtUsed;
  gFtUsed += need;
  return p;
}

static void FtFree(FT_Memory memory, void *block) {
  (void)memory;
  (void)block;
}

static void *FtRealloc(FT_Memory memory, long cur_size, long new_size,
                       void *block) {
  void *p;
  unsigned long n;
  unsigned long i;
  unsigned char *d;
  unsigned char *s;

  if (new_size <= 0) {
    return 0;
  }
  p = FtAlloc(memory, new_size);
  if (p && block && cur_size > 0) {
    n = (unsigned long)cur_size;
    if ((unsigned long)new_size < n) {
      n = (unsigned long)new_size;
    }
    d = (unsigned char *)p;
    s = (unsigned char *)block;
    for (i = 0; i < n; i++) {
      d[i] = s[i];
    }
  }
  return p;
}

FT_BASE_DEF(FT_Memory)
FT_New_Memory(void) {
  FT_Memory memory;

  gFtUsed = 0;
  memory = (FT_Memory)FtAlloc(0, (long)sizeof(*memory));
  if (!memory) {
    return 0;
  }
  memory->user = 0;
  memory->alloc = FtAlloc;
  memory->realloc = FtRealloc;
  memory->free = FtFree;
  return memory;
}

FT_BASE_DEF(void)
FT_Done_Memory(FT_Memory memory) {
  (void)memory;
  gFtUsed = 0;
}
