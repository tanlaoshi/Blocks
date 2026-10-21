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

static FT_Library gLib;
static FT_Face gFace;
static FT_Face gMonoFace;
static int gFtOk;
static int gFtMonoOk;
static UINT32 gFtLastAdv;

int FontFtInit(void) {
  const UINT8 *Blob;
  UINT32 Size;
  FT_Error Err;

  gFtOk = 0;
  gFtMonoOk = 0;
  gLib = 0;
  gFace = 0;
  gMonoFace = 0;
  gFtLastAdv = 0;
  Blob = FontTtfBlob(&Size);
  if (!Blob || Size < 12u || !HalFpuOk()) {
    LogBoot("Boot: ft init skip\n");
    return -1;
  }
  if (!HalFpuBegin()) {
    LogBoot("Boot: ft init skip\n");
    return -1;
  }
  Err = FT_Init_FreeType(&gLib);
  if (Err) {
    HalFpuEnd();
    LogBoot("Boot: ft init fail\n");
    return -1;
  }
  Err = FT_New_Memory_Face(gLib, Blob, (FT_Long)Size, 0, &gFace);
  if (Err || !gFace) {
    FT_Done_FreeType(gLib);
    gLib = 0;
    HalFpuEnd();
    LogBoot("Boot: ft face fail\n");
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
  LogBoot(gFtMonoOk ? "Boot: ft init ok mono\n" : "Boot: ft init ok\n");
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

int FontFtRasterCp(UINT32 Cp, UINT8 *Pix, UINT32 CellW, UINT32 CellH) {
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
  if (!Pix || CellW == 0 || CellH == 0 || Cp < 32u || Cp == 127u) {
    return -1;
  }
  Mono = FontTtfMonoActive() && gFtMonoOk && Cp < 128u;
  Face = Mono ? gMonoFace : gFace;
  if (!Face || (!gFtOk && !Mono)) {
    return -1;
  }
  MaxPix = CellW * CellH;
  for (i = 0; i < MaxPix; i++) {
    Pix[i] = 0;
  }
  if (!HalFpuBegin()) {
    return -1;
  }
  Err = FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)CellH);
  if (Err) {
    HalFpuEnd();
    return -1;
  }
  /*
   * font-freetype-hint：
   * - 汉字（UMing）：FORCE_AUTOHINT，桌面已验更锐
   * - mono（DejaVu）：走 native TT hint；强行 autohint 观感几乎无差
   */
  {
    int Flags = FT_LOAD_RENDER;

    if (!Mono) {
      Flags |= FT_LOAD_FORCE_AUTOHINT;
    }
    Err = FT_Load_Char(Face, (FT_ULong)Cp, Flags);
  }
  if (Err) {
    HalFpuEnd();
    return -1;
  }
  Slot = Face->glyph;
  Bm = &Slot->bitmap;
  if (Bm->pixel_mode != FT_PIXEL_MODE_GRAY || !Bm->buffer) {
    HalFpuEnd();
    return -1;
  }
  AscPx = (int)(Face->size->metrics.ascender >> 6);
  if (AscPx < 0) {
    AscPx = 0;
  }
  Oy = AscPx - Slot->bitmap_top;
  Ox = Slot->bitmap_left;
  if (Mono) {
    /* 等宽格：水平用 bitmap_left，超出则夹进格内 */
    if (Ox < 0) {
      Ox = 0;
    }
  } else if (Cp < 128u) {
    /*
     * UI 拉丁：缓存槽仍是 Side×Side，步进约半宽；右半必须留空，
     * 否则下一字 Adv=Side/2 会叠在本字墨水上（汉字方框字体尤甚）。
     */
    Ox = 0;
    if ((int)Bm->width > (int)(CellW / 2u) && CellW >= 2u) {
      /* 宽字母：仍靠左，允许少量伸进右半，步进用真实 advance */
      Ox = 0;
    }
  }
  for (Y = 0; Y < Bm->rows; Y++) {
    int Dy = Oy + (int)Y;
    if (Dy < 0 || Dy >= (int)CellH) {
      continue;
    }
    for (X = 0; X < Bm->width; X++) {
      int Dx = Ox + (int)X;
      if (Dx < 0 || Dx >= (int)CellW) {
        continue;
      }
      Pix[(UINT32)Dy * CellW + (UINT32)Dx] =
          Bm->buffer[Y * (UINT32)Bm->pitch + X];
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
  HalFpuEnd();
  return 0;
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
