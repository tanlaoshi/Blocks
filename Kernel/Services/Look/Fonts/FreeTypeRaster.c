/*
 * FreeTypeRaster.c — FreeType 栅格（hook + hint）。
 * font-freetype-hint：FT_LOAD_FORCE_AUTOHINT；失败由调用方软退 stb。
 */
#include "Font.h"
#include "Hal.h"
#include "SerialLog.h"

#if defined(__x86_64__) && defined(FONTS_HAVE_FREETYPE)

#include <ft2build.h>
#include FT_FREETYPE_H

#include "ftarena.h"

static FT_Library gLib;
static FT_Face gFace[FONT_TTF_UI_MAX];
static UINT32 gFtFaceMask;
static FT_Face gMonoFace;
static int gFtOk;
static int gFtMonoOk;
/*
 * freetype-port 的 FT_Memory 现在是 256KiB 静态 arena + 首次适配 free-list
 * （ThirdParty/freetype-port/ftsystem_blocks.c）：free 会按物理前后邻合并，
 * realloc 先试原地，回收的内存立即复用。旧版 FtFree 是空实现：开机预热
 * 到 cp=24212（U+5E94「应」）时 256KiB bump arena 已灌满，此后
 * FT_Load_Char 全返回 FT_Err_Out_Of_Memory，整个会话退 stb。
 *
 * FT 调用仍用自有自旋锁串行化（FT 库非线程安全，Gui/Worker 都会进来）。
 * OOM 不再一发生就整会话停用：只打一行 arena 统计并让本次字形退 stb；
 * 连续 64 次 OOM（期间一次都没成功）才把 gFtDead 置 1 并丢 HIT 兜底，
 * 防止 arena 真耗尽时每个字都白跑一趟 FT。
 */
static volatile int gFtLock;
static int gFtDead;
static UINT32 gFtOomStreak;
static UINT32 gFtLastAdv;

/* 连续 OOM 达到这个数才判 FT 不可用（单次 OOM 只降级当前字形）。 */
#define FT_OOM_DISABLE_STREAK 64u
/* FtArenaLog 的「无此参数」哨兵 */
#define FT_LOG_NONE 0xFFFFFFFFu

/*
 * 临时自检：改 1 重编后，开机连续栅 5000 码点 × 3 字号（共 15000 次
 * FT_Load_Char），每 1000 次打印 arena 用量并做全量块链自检。默认 0，
 * 不占开机时间；真机复验时再开。
 */
#define FT_ARENA_SELFTEST 0

static void FtLock(void) {
  while (__sync_lock_test_and_set(&gFtLock, 1)) {
  }
}

static void FtUnlock(void) {
  __sync_lock_release(&gFtLock);
}

static char *FtPutStr(char *P, const char *S) {
  while (*S) {
    *P++ = *S++;
  }
  return P;
}

static char *FtPutU32(char *P, UINT32 V) {
  char T[10];
  int N = 0;

  if (V == 0u) {
    *P++ = '0';
    return P;
  }
  while (V != 0u) {
    T[N++] = (char)('0' + (int)(V % 10u));
    V /= 10u;
  }
  while (N > 0) {
    *P++ = T[--N];
  }
  return P;
}

/*
 * 一行 arena 统计：used/peak 用来判断「稳定波动」还是「只增不减」，
 * fail 是 FT_Memory 分配失败次数，bad 是块链/坏指针自检计数（正常 0）。
 */
static void FtArenaLog(const char *Tag, UINT32 A, UINT32 B) {
  char Line[192];
  char *P = Line;
  unsigned long Total;
  unsigned long Used;
  unsigned long Peak;
  unsigned long Fail;
  unsigned long Allocs;
  unsigned long Frees;
  unsigned long Bad;

  FtArenaStats(&Total, &Used, &Peak, &Fail);
  FtArenaOps(&Allocs, &Frees);
  Bad = FtArenaBadness();
  P = FtPutStr(P, "FONT ftarena ");
  P = FtPutStr(P, Tag);
  if (A != FT_LOG_NONE) {
    P = FtPutStr(P, " a=");
    P = FtPutU32(P, A);
  }
  if (B != FT_LOG_NONE) {
    P = FtPutStr(P, " b=");
    P = FtPutU32(P, B);
  }
  P = FtPutStr(P, " used=");
  P = FtPutU32(P, (UINT32)Used);
  P = FtPutStr(P, " peak=");
  P = FtPutU32(P, (UINT32)Peak);
  P = FtPutStr(P, " fail=");
  P = FtPutU32(P, (UINT32)Fail);
  P = FtPutStr(P, " bad=");
  P = FtPutU32(P, (UINT32)Bad);
  P = FtPutStr(P, " alloc=");
  P = FtPutU32(P, (UINT32)Allocs);
  P = FtPutStr(P, " free=");
  P = FtPutU32(P, (UINT32)Frees);
  P = FtPutStr(P, " total=");
  P = FtPutU32(P, (UINT32)Total);
  *P++ = '\r';
  *P++ = '\n';
  *P = 0;
  HalSerialWriteShell(Line);
}

#if FT_ARENA_SELFTEST
/*
 * 连续栅 5000 个真实 cmap 码点 × 3 种字号，共 15000 次 FT_Load_Char。
 * 每次按当前 Face 的 cmap 顺序取字，走完一轮从头再来；每 1000 次打印
 * used/peak/fail 并跑一次 FtArenaCheck（物理块链 + 空闲链 + 记账）。
 * 判据：oom=0，check 全绿，预热后 used 在一个窄区间里波动。
 */
static int FtArenaSelfTest(void) {
  static const FT_UInt Sizes[3] = {18u, 32u, 48u};
  FT_Face Face;
  FT_UInt GlyphIndex = 0u;
  FT_ULong Cp;
  FT_Error Err;
  UINT32 Loaded = 0u;
  UINT32 OomN = 0u;
  UINT32 S;
  UINT32 I;
  unsigned long MinUsed = 0xFFFFFFFFul;
  unsigned long MaxUsed = 0ul;
  int Rc = 0;

  if ((gFtFaceMask & 1u) == 0u || !gFace[0]) {
    return -1;
  }
  Face = gFace[0];
  if (!HalFpuBegin()) {
    return -1;
  }
  for (S = 0; S < 3u && Rc == 0; S++) {
    if (FT_Set_Pixel_Sizes(Face, 0, Sizes[S])) {
      Rc = -1;
      break;
    }
    Cp = (FT_ULong)FT_Get_First_Char(Face, &GlyphIndex);
    if (GlyphIndex == 0u) {
      Rc = -1;
      break;
    }
    for (I = 0; I < 5000u; I++) {
      UINT32 Guard = 0u;

      while (Guard < 200000u) {
        if (Cp >= 32u && Cp != 127u &&
            FT_Get_Char_Index(Face, Cp) != 0u) {
          break;
        }
        Cp = (FT_ULong)FT_Get_Next_Char(Face, Cp, &GlyphIndex);
        if (GlyphIndex == 0u) {
          Cp = (FT_ULong)FT_Get_First_Char(Face, &GlyphIndex);
        }
        Guard++;
      }
      if (Guard >= 200000u) {
        Rc = -1;
        break;
      }
      Err = FT_Load_Char(Face, Cp, FT_LOAD_RENDER);
      if (Err == FT_Err_Out_Of_Memory) {
        OomN++;
      }
      Loaded++;
      if ((Loaded % 1000u) == 0u) {
        unsigned long T;
        unsigned long U;
        unsigned long Pk;
        unsigned long F;

        FtArenaStats(&T, &U, &Pk, &F);
        FtArenaLog("step", Loaded, FtArenaCheck() ? 1u : 0u);
        if (Loaded > 3000u) {
          if (U < MinUsed) {
            MinUsed = U;
          }
          if (U > MaxUsed) {
            MaxUsed = U;
          }
        }
      }
      Cp = (FT_ULong)FT_Get_Next_Char(Face, Cp, &GlyphIndex);
      if (GlyphIndex == 0u) {
        Cp = (FT_ULong)FT_Get_First_Char(Face, &GlyphIndex);
      }
    }
  }
  HalFpuEnd();
  {
    char Line[112];
    char *P = Line;

    P = FtPutStr(P, "FONT ft selftest glyphs=");
    P = FtPutU32(P, Loaded);
    P = FtPutStr(P, " oom=");
    P = FtPutU32(P, OomN);
    P = FtPutStr(P, " used-min=");
    P = FtPutU32(P, (UINT32)MinUsed);
    P = FtPutStr(P, " used-max=");
    P = FtPutU32(P, (UINT32)MaxUsed);
    P = FtPutStr(P, Rc == 0 ? " ok\r\n" : " FAIL\r\n");
    *P = 0;
    HalSerialWriteShell(Line);
  }
  return Rc;
}
#endif

static int FtInitLocked(void);

int FontFtInit(void) {
  int Rc;

  FtLock();
  Rc = FtInitLocked();
  FtUnlock();
  return Rc;
}

static int FtInitLocked(void) {
  const UINT8 *Blob;
  UINT32 Size;
  UINT32 Slot;
  FT_Error Err;
  int Any = 0;

  /*
   * 重开：FT_Done_FreeType → FT_Done_Memory 会把 FT 对象逐个 free 回
   * free-list；万一还有残留，FT_Done_Memory 兜底整块归零。之后这里不
   * 再手工清 Used，正常分配/回收就能复用同一个 arena。
   */
  if (gLib) {
    FT_Done_FreeType(gLib);
    gLib = 0;
  }
  gFtOk = 0;
  gFtMonoOk = 0;
  gFtFaceMask = 0;
  gLib = 0;
  gFtDead = 0;
  gFtOomStreak = 0;
  gMonoFace = 0;
  gFtLastAdv = 0;
  for (Slot = 0; Slot < FONT_TTF_UI_MAX; Slot++) {
    gFace[Slot] = 0;
  }
  if (!HalFpuOk()) {
    LogDrv("Boot: ft init skip\n");
    return -1;
  }
  Blob = FontTtfUiBlob(0, &Size);
  if (!Blob || Size < 12u) {
    LogDrv("Boot: ft init skip\n");
    return -1;
  }
  if (!HalFpuBegin()) {
    LogDrv("Boot: ft init skip\n");
    return -1;
  }
  Err = FT_Init_FreeType(&gLib);
  if (Err) {
    HalFpuEnd();
    LogDrv("Boot: ft init fail\n");
    return -1;
  }
  for (Slot = 0; Slot < FONT_TTF_UI_MAX; Slot++) {
    Size = 0;
    Blob = FontTtfUiBlob(Slot, &Size);
    if (!Blob || Size < 12u) {
      continue;
    }
    Err = FT_New_Memory_Face(gLib, Blob, (FT_Long)Size, 0, &gFace[Slot]);
    if (!Err && gFace[Slot]) {
      gFtFaceMask |= (1u << Slot);
      Any = 1;
    } else {
      gFace[Slot] = 0;
    }
  }
  if (!Any) {
    FT_Done_FreeType(gLib);
    gLib = 0;
    HalFpuEnd();
    LogDrv("Boot: ft face fail\n");
    return -1;
  }
  gFtOk = 1;
  Blob = FontTtfMonoBlob(&Size);
  if (Blob && Size >= 12u) {
    Err = FT_New_Memory_Face(gLib, Blob, (FT_Long)Size, 0, &gMonoFace);
    if (!Err && gMonoFace) {
      gFtMonoOk = 1;
    }
  }
  HalFpuEnd();
#if FT_ARENA_SELFTEST
  (void)FtArenaSelfTest();
#endif
  FtArenaLog("init", FT_LOG_NONE, FT_LOG_NONE);
  LogDrv(gFtMonoOk ? "Boot: ft init ok mono\n" : "Boot: ft init ok\n");
  return 0;
}

int FontFtReady(void) {
  return gFtOk ? 1 : 0;
}

UINT32 FontFtLastAdvance(void) {
  return gFtLastAdv;
}

void FontFtClearLastAdvance(void) {
  gFtLastAdv = 0;
}

static UINT32 gFtFailN;

static void FtFail(UINT32 Cp, UINT32 Reason) {
  static UINT32 Seen;
  char Line[48];
  char *P = Line;
  const char *Pre = "FONT ftfail reason=";

  if (gFtFailN >= 40u && (Seen & (1u << Reason))) {
    return;
  }
  gFtFailN++;
  Seen |= 1u << Reason;
  while (*Pre) {
    *P++ = *Pre++;
  }
  {
    char R[10];
    int n = 0;

    if (Reason == 0u) {
      R[n++] = '0';
    }
    while (Reason != 0u) {
      R[n++] = (char)('0' + (int)(Reason % 10u));
      Reason /= 10u;
    }
    while (n > 0) {
      *P++ = R[--n];
    }
  }
  {
    const char *Mid = " cp=";

    while (*Mid) {
      *P++ = *Mid++;
    }
  }
  {
    char T[10];
    int n = 0;

    if (Cp == 0u) {
      T[n++] = '0';
    }
    while (Cp != 0u) {
      T[n++] = (char)('0' + (int)(Cp % 10u));
      Cp /= 10u;
    }
    while (n > 0) {
      *P++ = T[--n];
    }
  }
  *P++ = '\r';
  *P++ = '\n';
  *P = 0;
  HalSerialWriteShell(Line);
}

static void FtOom(UINT32 Cp) {
  gFtOomStreak++;
  /* 单次 OOM 只降级当前字形：打一行统计（used/peak/fail）便于定位。 */
  FtArenaLog("oom", Cp, gFtOomStreak);
  if (gFtOomStreak < FT_OOM_DISABLE_STREAK) {
    return;
  }
  /*
   * 连续 64 次 OOM（期间没有一次成功）才判 arena 真耗尽：停用 FT 并丢 HIT，
   * 避免每个字都白跑一趟。已经用 FT 栅过的字形是「另一条路」的位图，
   * 整片作废后按 stb 重栅，避免同一屏混排两种栅格器。
   * 注意：这里是 FillSlot 内部，DropHits 抬了代数 → 本次写入会被
   * FillSlot 的代次校验丢掉，不会把旧位图写回。
   */
  gFtDead = 1;
  FtArenaLog("disable", Cp, gFtOomStreak);
  FontTtfCacheDropHits();
}

static int FtRasterBody(UINT32 Cp, UINT8 *Pix, UINT32 CellW, UINT32 CellH) {
  FT_Face Face;
  FT_Error Err;
  FT_GlyphSlot Slot;
  FT_Bitmap *Bm;
  UINT32 X;
  UINT32 Y;
  UINT32 i;
  int Ox;
  int Oy;
  int Mono;
  int AscPx;
  UINT32 Adv;
  UINT32 MaxPix;

  gFtLastAdv = 0;
  if (gFtDead) {
    return -1;
  }
  if (!Pix || CellW == 0 || CellH == 0 || Cp < 32u || Cp == 127u) {
    FtFail(Cp, 1u);
    return -1;
  }
  /*
   * 空格没有墨：FT 的空位图 buffer==NULL，原逻辑会落到 reason=7 并把
   * 每个空格都记一次失败（FT 复活后开机 40 行噪音）。直接交 stb 出空白，
   * 与旧行为（FT 已死时）一致，也省一次 FT_Load_Char。
   */
  if (Cp == 32u) {
    return -1;
  }
  Mono = FontTtfMonoActive() && gFtMonoOk && Cp < 128u;
  if (Mono) {
    Face = gMonoFace;
  } else {
    UINT32 Slot = FontTtfUiActiveSlot();

    if (Slot >= FONT_TTF_UI_MAX || (gFtFaceMask & (1u << Slot)) == 0u) {
      Slot = 0;
    }
    if ((gFtFaceMask & (1u << Slot)) == 0u) {
      for (Slot = 0; Slot < FONT_TTF_UI_MAX; Slot++) {
        if (gFtFaceMask & (1u << Slot)) {
          break;
        }
      }
    }
    Face = (Slot < FONT_TTF_UI_MAX) ? gFace[Slot] : 0;
  }
  if (!Face || (!gFtOk && !Mono)) {
    FtFail(Cp, 2u);
    return -1;
  }
  MaxPix = CellW * CellH;
  for (i = 0; i < MaxPix; i++) {
    Pix[i] = 0;
  }
  if (!HalFpuBegin()) {
    FtFail(Cp, 3u);
    return -1;
  }
  /*
   * 汉字：满格 ppem。
   * UI 拉丁：整数加档 +1/4 ppem 重栅格，禁非整数位图拉伸。
   * PR-23E / 坑 2：提权上限改成与格高成比例（CellH+CellH/4：100%→30、
   * 150%→45、200%→60），不再写 32×整数绘制倍数——那条式子把「逻辑上限」
   * 和「绘制倍数」混在一起，150% 比例化后必然对不上号。
   * 这里再封一层 64（= TrueTypeCache/TrueTypeRaster 的 TTF_CELL，槽边长），
   * 免得格更大时 em 超出缓存槽。50/100/150/200 下上限都取不到，行为不变。
   * 中英共用 CellH 收进基线 SharedAsc：ascent+descent 超格则上移基线留底，
   * 避免 Droid 汉字下横被裁；加档只放大拉丁、不改共用基线。
   */
  {
    UINT32 ReqH = CellH;
    UINT32 ReqCap = CellH + (CellH / 4u);

    if (ReqCap > 64u) {
      ReqCap = 64u;
    }
    int SharedAsc = 0;
    int DescPx = 0;

    if (!Mono) {
      Err = FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)CellH);
      if (Err) {
        FtFail(Cp, 400u + (UINT32)Err);
        HalFpuEnd();
        return -1;
      }
      AscPx = (int)(Face->size->metrics.ascender >> 6);
      DescPx = -(int)(Face->size->metrics.descender >> 6);
      if (AscPx < 0) {
        AscPx = 0;
      }
      if (DescPx < 1) {
        /* Droid/UMing 汉字常比 metrics 降部更深，至少留 2px 底 */
        DescPx = 2;
      }
      if (AscPx + DescPx > (int)CellH) {
        SharedAsc = (int)CellH - DescPx;
      } else {
        SharedAsc = AscPx;
      }
      if (SharedAsc < 1) {
        SharedAsc = (int)CellH * 3 / 4;
      }
      if (SharedAsc >= (int)CellH) {
        SharedAsc = (int)CellH - 1;
      }
      if (Cp < 128u) {
        ReqH = CellH + (CellH / 4u);
        if (ReqH > ReqCap) {
          ReqH = ReqCap;
        }
        if (ReqH < CellH) {
          ReqH = CellH;
        }
        if (ReqH != CellH) {
          Err = FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)ReqH);
          if (Err) {
            HalFpuEnd();
            return -1;
          }
        }
      }
    } else {
      Err = FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)CellH);
      if (Err) {
        HalFpuEnd();
        return -1;
      }
    }
    /*
     * cjk-3：无 cmap 项勿 Load 成 .notdef 当 HIT（静默空白/框装过关）。
     * mono ASCII 缺字仍失败返回，由 Terminus 软退。
     */
    if (FT_Get_Char_Index(Face, (FT_ULong)Cp) == 0) {
      FtFail(Cp, 5u);
      HalFpuEnd();
      return -1;
    }
    /*
     * font-freetype-hint / cjk-5 / 拉丁光学：
     * - 汉字、UI 拉丁：TARGET_NORMAL（笔画实度同级；LIGHT 拉丁偏瘦显更小）
     * - mono：TARGET_LIGHT（近 Windows 灰度 AA；禁 FORCE_AUTOHINT）
     */
    {
      int Flags = FT_LOAD_RENDER;

      if (Mono) {
        Flags |= FT_LOAD_TARGET_LIGHT;
      }
      Err = FT_Load_Char(Face, (FT_ULong)Cp, Flags);
    }
    if (Err) {
      if (Err == FT_Err_Out_Of_Memory) {
        FtOom(Cp);
      }
      HalFpuEnd();
      return -1;
    }
    Slot = Face->glyph;
    Bm = &Slot->bitmap;
    if (Bm->pixel_mode != FT_PIXEL_MODE_GRAY || !Bm->buffer) {
      FtFail(Cp, 7u);
      HalFpuEnd();
      return -1;
    }
    AscPx = (int)(Face->size->metrics.ascender >> 6);
    if (AscPx < 0) {
      AscPx = 0;
    }
    if (Mono) {
      /* 等宽：VMetrics 基线；水平夹进格内 */
      Oy = AscPx - Slot->bitmap_top;
      Ox = Slot->bitmap_left;
      if (Ox < 0) {
        Ox = 0;
      }
    } else if (Cp >= 128u) {
      /* UI 汉字：共用 SharedAsc；墨仍超底则整字上移进格（保下横） */
      Ox = ((int)CellW - (int)Bm->width) / 2;
      Oy = SharedAsc - Slot->bitmap_top;
      if (Ox < 0) {
        Ox = 0;
      }
      if ((int)Bm->rows <= (int)CellH) {
        if (Oy + (int)Bm->rows > (int)CellH) {
          Oy = (int)CellH - (int)Bm->rows;
        }
        if (Oy < 0) {
          Oy = 0;
        }
      }
    } else {
      /* UI 拉丁：同一 SharedAsc + ReqH 字形；超格只 blit 裁 */
      Oy = SharedAsc - Slot->bitmap_top;
      Ox = 0;
    }
  }
  {
    UINT32 Ink = 0;

    for (Y = 0; Y < Bm->rows; Y++) {
      int Dy = Oy + (int)Y;
      if (Dy < 0 || Dy >= (int)CellH) {
        continue;
      }
      for (X = 0; X < Bm->width; X++) {
        int Dx = Ox + (int)X;
        UINT8 V;

        if (Dx < 0 || Dx >= (int)CellW) {
          continue;
        }
        V = Bm->buffer[Y * (UINT32)Bm->pitch + X];
        Pix[(UINT32)Dy * CellW + (UINT32)Dx] = V;
        /* 拉丁/mono 用低阈值计墨；汉字仍 88（与 GlyphCrisp 一致） */
        if (V >= ((Mono || Cp < 128u) ? 24u : 88u)) {
          Ink++;
        }
      }
    }
    /* 空心 HIT 会步进却不画；一律软退 stb（含 UI 拉丁） */
    if (Ink == 0u) {
      FtFail(Cp, 8u);
      HalFpuEnd();
      gFtLastAdv = 0;
      return -1;
    }
  }
  Adv = (UINT32)(Slot->advance.x >> 6);
  if (Adv < 1u) {
    Adv = Mono ? CellW : (Cp < 128u ? (CellW / 2u) : CellW);
  }
  if (Adv > CellW) {
    Adv = CellW;
  }
  gFtLastAdv = Adv;
  gFtOomStreak = 0;
  HalFpuEnd();
  return 0;
}

int FontFtRasterCp(UINT32 Cp, UINT8 *Pix, UINT32 CellW, UINT32 CellH) {
  int Rc;

  /*
   * PR-22：FT 库不是线程安全的，而 FillSlot 既跑在 Gui 线程（同步灌格）
   * 也跑在 Worker（WantDrain）。开机的 FPU 禁抢占只挡住本 CPU 的切换，
   * 真机多核上两边会同时进 FT → 大小对象/字形缓冲互相踩。这里用自旋锁
   * 把 FT 调用串起来（临界区内仍按原样 HalFpuBegin/End）。
   */
  FtLock();
  Rc = FtRasterBody(Cp, Pix, CellW, CellH);
  FtUnlock();
  return Rc;
}

#else

int FontFtInit(void) {
  return -1;
}

int FontFtReady(void) {
  return 0;
}

UINT32 FontFtLastAdvance(void) {
  return 0;
}

void FontFtClearLastAdvance(void) {
}

int FontFtRasterCp(UINT32 Cp, UINT8 *Pix, UINT32 CellW, UINT32 CellH) {
  (void)Cp;
  (void)Pix;
  (void)CellW;
  (void)CellH;
  return -1;
}

#endif
