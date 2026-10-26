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
static FT_Face gFace[FONT_TTF_UI_MAX];
static UINT32 gFtFaceMask;
static FT_Face gMonoFace;
static int gFtOk;
static int gFtMonoOk;
static UINT32 gFtLastAdv;

int FontFtInit(void) {
  const UINT8 *Blob;
  UINT32 Size;
  UINT32 Slot;
  FT_Error Err;
  int Any = 0;

  gFtOk = 0;
  gFtMonoOk = 0;
  gFtFaceMask = 0;
  gLib = 0;
  gMonoFace = 0;
  gFtLastAdv = 0;
  for (Slot = 0; Slot < FONT_TTF_UI_MAX; Slot++) {
    gFace[Slot] = 0;
  }
  if (!HalFpuOk()) {
    LogBoot("Boot: ft init skip\n");
    return -1;
  }
  Blob = FontTtfUiBlob(0, &Size);
  if (!Blob || Size < 12u) {
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
    return -1;
  }
  MaxPix = CellW * CellH;
  for (i = 0; i < MaxPix; i++) {
    Pix[i] = 0;
  }
  if (!HalFpuBegin()) {
    return -1;
  }
  /* 满格像素高；格内居中，勿再 CellH-2（缩后观感接近旧 stb，像「没换字」）。 */
  Err = FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)CellH);
  if (Err) {
    HalFpuEnd();
    return -1;
  }
  /*
   * cjk-3：无 cmap 项勿 Load 成 .notdef 当 HIT（静默空白/框装过关）。
   * mono ASCII 缺字仍失败返回，由 Terminus 软退。
   */
  if (FT_Get_Char_Index(Face, (FT_ULong)Cp) == 0) {
    HalFpuEnd();
    return -1;
  }
  /*
   * font-freetype-hint / cjk-5：
   * - 汉字：TARGET_NORMAL（笔画更实；LIGHT 偏瘦）
   * - mono / UI 拉丁：TARGET_LIGHT（近 Windows 灰度 AA；禁 FORCE_AUTOHINT）
   */
  {
    int Flags = FT_LOAD_RENDER;

    if (Mono || Cp < 128u) {
      Flags |= FT_LOAD_TARGET_LIGHT;
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
  if (Mono) {
    /* 等宽：VMetrics 基线；水平夹进格内 */
    Oy = AscPx - Slot->bitmap_top;
    Ox = Slot->bitmap_left;
    if (Ox < 0) {
      Ox = 0;
    }
  } else if (Cp >= 128u) {
    /*
     * UI 汉字：与 stb 格内居中一致。纯基线时 UMing 常 Oy+rows>CellH → 底被削。
     * 仍超高则顶齐（宁可削顶，桌面标签少见）。
     */
    Ox = ((int)CellW - (int)Bm->width) / 2;
    Oy = ((int)CellH - (int)Bm->rows) / 2;
    if (Ox < 0) {
      Ox = 0;
    }
    if (Oy < 0) {
      Oy = 0;
    }
    if (Oy + (int)Bm->rows > (int)CellH && (int)Bm->rows <= (int)CellH) {
      Oy = (int)CellH - (int)Bm->rows;
    }
  } else {
    /* UI 拉丁（若走 FT）：靠左，步进半宽防叠字 */
    Oy = AscPx - Slot->bitmap_top;
    Ox = 0;
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
