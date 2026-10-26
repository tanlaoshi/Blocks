/*
 * Common/Fonts/FontRegistry.c — 字体注册表与当前字体（PR-D1）+ Assets/Fonts TOYF（PR-T3）
 *
 * TOYF v1（小端）：
 *   0  magic "TOYF"
 *   4  version=1, scale, char_spacing, line_spacing  (4×u8)
 *   8  width, height, first_char, glyph_count         (4×u16 LE)
 *  16  name[16] NUL 填充
 *  32  glyph bytes = glyph_count * height * ((width+7)/8)
 */
#include "Font.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "LibWrite.h"
#include "SerialLog.h"

#define FONT_TOYF_MAGIC   0x46594F54u /* 'TOYF' LE */
#define FONT_TOYF_VERSION 1u
#define FONT_TOYF_HDR     32u
#define FONT_FILE_MAX     (64u * 1024u)
/* 系统字占 0..FONT_APP_SLOT-1，来自 Assets/Fonts 下的 FNT；末槽专供应用私有字 */
#define FONT_RUNTIME_MAX  8u
#define FONT_APP_SLOT     (FONT_RUNTIME_MAX - 1u)
#define FONT_NAME_MAX     16u
#define FONT_TABLE_MAX    8u
#define FONT_SPACING_MAX  16u
#define FONT_SCALE_MAX    8u

typedef struct FONT_RUNTIME_SLOT {
    FONT_FACE Face;
    char Name[FONT_NAME_MAX];
    UINT8 *Pages;
    UINT32 PageCount;
    int Used;
} FONT_RUNTIME_SLOT;

static FONT_RUNTIME_SLOT gRuntime[FONT_RUNTIME_MAX];
static const FONT_FACE *gFonts[FONT_TABLE_MAX];
static UINT32 gFontCount;
static UINT32 gCurrentId;
static UINT32 gGlyphCpBpp;
static int gGlyphCrispLatin; /* cjk-5：TTF 拉丁灰图 → 软 AA */
static UINT32 gUiTtfSlot; /* 0=UMING.TTF；1=DROID.TTF */

/*
 * cjk-4/5：设置→字体列表里的 UI TTF 项（无点阵 Glyphs）。
 * 选中改中文栅格槽；cjk-5 起 ASCII 同文件 TTF 拉丁（软 AA，勿汉字硬 crisp）。
 */
/* 列表短名（详情/theme 也用）；全称见 UMING.TTF / DROID.TTF 文件 */
static const FONT_FACE gFaceUiUming = {
    .Name = "UMing",
    .Width = 10,
    .Height = 18,
    .BytesPerGlyph = 0,
    .BytesPerRow = 0,
    .CharSpacing = 1,
    .LineSpacing = 2,
    .Scale = 1,
    .Glyphs = 0,
    .GlyphCount = 0,
    .FirstChar = 32,
};

static const FONT_FACE gFaceUiDroid = {
    .Name = "Droid",
    .Width = 10,
    .Height = 18,
    .BytesPerGlyph = 0,
    .BytesPerRow = 0,
    .CharSpacing = 1,
    .LineSpacing = 2,
    .Scale = 1,
    .Glyphs = 0,
    .GlyphCount = 0,
    .FirstChar = 32,
};

UINT32 FontTtfUiActiveSlot(void) {
    return gUiTtfSlot < FONT_TTF_UI_MAX ? gUiTtfSlot : 0;
}

int FontUiTtfSelected(void) {
    const FONT_FACE *F = (gCurrentId < gFontCount) ? gFonts[gCurrentId] : 0;

    return (F && F->Glyphs == 0) ? 1 : 0;
}

int FontGlyphCrispLatin(void) {
    return gGlyphCrispLatin;
}

/* UI TTF 项 Glyphs=0：点阵路径软退 Terminus（勿用空 face BytesPerRow=0 去 Paint） */
const FONT_FACE *FontGetLatinFace(void) {
    const FONT_FACE *F = (gCurrentId < gFontCount) ? gFonts[gCurrentId] : 0;

    if (F && F->Glyphs != 0 && F->BytesPerGlyph != 0) {
        return F;
    }
    return &gFontFaceTerminus10x18;
}

static const FONT_FACE *FontFaceForLatin(void) {
    return FontGetLatinFace();
}

static UINT16 RdU16(const UINT8 *P) {
    return (UINT16)P[0] | ((UINT16)P[1] << 8);
}

static UINT32 RdU32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

static char ToUpperAscii(char C) {
    if (C >= 'a' && C <= 'z') {
        return (char)(C - 'a' + 'A');
    }
    return C;
}

static int NameEqCi(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *B) {
        if (ToUpperAscii(*A) != ToUpperAscii(*B)) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

/* 已注册（内建 UMing/Terminus + 已占用运行时槽，不含 Candidate） */
static int FaceNameRegistered(const char *Name, const FONT_RUNTIME_SLOT *Candidate) {
    UINT32 i;

    if (!Name || Name[0] == 0) {
        return 0;
    }
    if (NameEqCi(Name, gFaceUiUming.Name) ||
        NameEqCi(Name, gFaceUiDroid.Name) ||
        NameEqCi(Name, gFontFaceTerminus10x18.Name)) {
        return 1;
    }
    for (i = 0; i < FONT_RUNTIME_MAX; i++) {
        if (!gRuntime[i].Used || &gRuntime[i] == Candidate) {
            continue;
        }
        if (NameEqCi(Name, gRuntime[i].Name)) {
            return 1;
        }
    }
    return 0;
}

static void RebuildFontTable(void) {
    UINT32 i;
    UINT32 BlobSize = 0;

    gFontCount = 0;
    /*
     * cjk-4：有 CJK*.TTF 则 UI TTF 置顶「中英」；无 TTF 时 id0=Terminus。
     */
    BlobSize = 0;
    if (FontTtfUiBlob(0, &BlobSize) && BlobSize >= 12u &&
        gFontCount < FONT_TABLE_MAX) {
        gFonts[gFontCount++] = &gFaceUiUming;
    }
    BlobSize = 0;
    if (FontTtfUiBlob(1, &BlobSize) && BlobSize >= 12u &&
        gFontCount < FONT_TABLE_MAX) {
        gFonts[gFontCount++] = &gFaceUiDroid;
    }
    if (gFontCount < FONT_TABLE_MAX) {
        gFonts[gFontCount++] = &gFontFaceTerminus10x18;
    }
    for (i = 0; i < FONT_RUNTIME_MAX; i++) {
        if (gRuntime[i].Used && gFontCount < FONT_TABLE_MAX) {
            gFonts[gFontCount++] = &gRuntime[i].Face;
        }
    }
}

void FontNotifyTtfReady(void) {
    UINT32 Cur = gCurrentId;

    RebuildFontTable();
    if (FontSetById(Cur) != 0 && FontCount() > 0u) {
        (void)FontSetById(0);
    }
}

void FontFaceLabel(UINT32 Id, char *Out, int OutMax) {
    const FONT_FACE *Face;
    const char *Name;
    const char *Tag;
    int i;
    int j;

    if (!Out || OutMax <= 0) {
        return;
    }
    Out[0] = 0;
    Face = FontGetById(Id);
    Name = (Face && Face->Name) ? Face->Name : "?";
    /*
     * cjk-4：中文后缀须先 Preheat，否则仅/英 defer →「（中 ）」「（  文）」空档。
     * 标签仅数个汉字，开设置时同步灌槽可接受。
     */
    Tag = (Face && Face->Glyphs == 0)
              ? "\xEF\xBC\x88\xE4\xB8\xAD\xE8\x8B\xB1\xEF\xBC\x89"
              : "\xEF\xBC\x88\xE4\xBB\x85\xE8\x8B\xB1\xE6\x96\x87\xEF\xBC\x89";
    for (i = 0; Name[i] && i + 1 < OutMax; i++) {
        Out[i] = Name[i];
    }
    for (j = 0; Tag[j] && i + 1 < OutMax; j++) {
        Out[i++] = Tag[j];
    }
    Out[i] = 0;
    FontTtfPreheatUtf8(Out);
}

static void FreeRuntimeSlot(FONT_RUNTIME_SLOT *S) {
    UINT8 *Pages;
    UINT32 PageCount;

    S->Face.Glyphs = 0;
    S->Used = 0;
    Pages = S->Pages;
    PageCount = S->PageCount;
    S->Pages = 0;
    S->PageCount = 0;
    S->Face.Name = S->Name;
    if (Pages && PageCount) {
        PhysicalMemoryFreePages(Pages, PageCount);
    }
}

static void ClearRuntime(void) {
    UINT32 i;

    for (i = 0; i < FONT_RUNTIME_MAX; i++) {
        FreeRuntimeSlot(&gRuntime[i]);
    }
    RebuildFontTable();
}

static int ParseToyf(const UINT8 *Buf, UINTN Size, FONT_RUNTIME_SLOT *Out) {
    UINT32 Magic;
    UINT32 Width;
    UINT32 Height;
    UINT32 First;
    UINT32 Count;
    UINT32 Bpr;
    UINT32 GlyphBytes;
    UINT32 Need;
    UINT32 i;
    UINT8 Scale;
    UINT8 CharSp;
    UINT8 LineSp;

    if (Size < FONT_TOYF_HDR) {
        return -1;
    }
    Magic = RdU32(Buf);
    if (Magic != FONT_TOYF_MAGIC || Buf[4] != FONT_TOYF_VERSION) {
        return -1;
    }
    Scale = Buf[5];
    CharSp = Buf[6];
    LineSp = Buf[7];
    Width = RdU16(Buf + 8);
    Height = RdU16(Buf + 10);
    First = RdU16(Buf + 12);
    Count = RdU16(Buf + 14);
    if (Scale == 0) {
        Scale = 1;
    }
    if (Scale > FONT_SCALE_MAX) {
        return -1;
    }
    if (Width == 0 || Width > 64 || Height == 0 || Height > 64 || Count == 0 ||
        Count > 256) {
        return -1;
    }
    if (CharSp > FONT_SPACING_MAX) {
        CharSp = FONT_SPACING_MAX;
    }
    if (LineSp > FONT_SPACING_MAX) {
        LineSp = FONT_SPACING_MAX;
    }
    Bpr = (Width + 7u) / 8u;
    GlyphBytes = Count * Height * Bpr;
    Need = FONT_TOYF_HDR + GlyphBytes;
    if (Size < Need) {
        return -1;
    }

    FreeRuntimeSlot(Out);
    Out->PageCount = (GlyphBytes + 4095u) / 4096u;
    if (Out->PageCount == 0) {
        Out->PageCount = 1;
    }
    Out->Pages = (UINT8 *)PhysicalMemoryAllocatePages(Out->PageCount);
    if (!Out->Pages) {
        Out->PageCount = 0;
        return -1;
    }
    for (i = 0; i < GlyphBytes; i++) {
        Out->Pages[i] = Buf[FONT_TOYF_HDR + i];
    }

    for (i = 0; i < FONT_NAME_MAX; i++) {
        Out->Name[i] = (char)Buf[16 + i];
    }
    Out->Name[FONT_NAME_MAX - 1] = 0;
    if (Out->Name[0] == 0) {
        Out->Name[0] = 'F';
        Out->Name[1] = 'N';
        Out->Name[2] = 'T';
        Out->Name[3] = 0;
    }

    Out->Face.Name = Out->Name;
    Out->Face.Width = Width;
    Out->Face.Height = Height;
    Out->Face.BytesPerGlyph = Height * Bpr;
    Out->Face.BytesPerRow = Bpr;
    Out->Face.CharSpacing = CharSp;
    Out->Face.LineSpacing = LineSp;
    Out->Face.Scale = Scale;
    Out->Face.Glyphs = Out->Pages;
    Out->Face.GlyphCount = Count;
    Out->Face.FirstChar = First;
    Out->Used = 1;
    return 0;
}

static int TryLoadPath(const char *Path, FONT_RUNTIME_SLOT *Slot) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    int Err;

    Pages = (FONT_FILE_MAX + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    Size = 0;
    Err = FileSystemReadFile(Path, Buf, FONT_FILE_MAX, &Size);
    if (Err != FAT_OK || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    Err = ParseToyf(Buf, Size, Slot);
    PhysicalMemoryFreePages(Buf, Pages);
    if (Err != 0) {
        LogGui("Font: Bad TOYF ");
        LogGui(Path);
        LogGui("\n");
        return -1;
    }
    LogGui("Font: Loaded ");
    LogGui(Path);
    LogGui(" (");
    LogGui(Slot->Name);
    LogGui(")\n");
    return 0;
}

void FontInitialize(void) {
    UINT32 i;

    gCurrentId = 0;
    for (i = 0; i < FONT_RUNTIME_MAX; i++) {
        gRuntime[i].Used = 0;
        gRuntime[i].Pages = 0;
        gRuntime[i].PageCount = 0;
        gRuntime[i].Face.Name = gRuntime[i].Name;
    }
    RebuildFontTable();
}

int FontLoadAssets(void) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    static char Path[96];
    UINT32 Slot;
    int Count = 0;
    int i;
    int Any = 0;
    int Err;

    ClearRuntime();
    Slot = 0;
    Err = FileSystemListEntries("Assets/Fonts", Ents, FAT_LIST_MAX, &Count);
    if (Err == FAT_OK) {
        for (i = 0; i < Count && Slot < FONT_APP_SLOT; i++) {
            int n;
            int j;

            if (Ents[i].Attr & FAT_ATTR_DIR) {
                continue;
            }
            n = 0;
            while (Ents[i].Name[n]) {
                n++;
            }
            if (n < 5) {
                continue;
            }
            if (!(Ents[i].Name[n - 4] == '.' &&
                  (Ents[i].Name[n - 3] == 'F' || Ents[i].Name[n - 3] == 'f') &&
                  (Ents[i].Name[n - 2] == 'N' || Ents[i].Name[n - 2] == 'n') &&
                  (Ents[i].Name[n - 1] == 'T' || Ents[i].Name[n - 1] == 't'))) {
                continue;
            }
            {
                const char *P = "Assets/Fonts/";
                int pi = 0;
                while (*P && pi + 1 < (int)sizeof(Path)) {
                    Path[pi++] = *P++;
                }
                j = 0;
                while (Ents[i].Name[j] && pi + 1 < (int)sizeof(Path)) {
                    Path[pi++] = Ents[i].Name[j++];
                }
                Path[pi] = 0;
            }
            if (TryLoadPath(Path, &gRuntime[Slot]) != 0) {
                continue;
            }
            if (FaceNameRegistered(gRuntime[Slot].Name, &gRuntime[Slot])) {
                LibWrite("Font: skip duplicate name ");
                LibWrite(gRuntime[Slot].Name);
                LibWrite(" (");
                LibWrite(Ents[i].Name);
                LibWrite(")\n");
                FreeRuntimeSlot(&gRuntime[Slot]);
                continue;
            }
            Slot++;
            Any = 1;
        }
    }

    RebuildFontTable();
    if (!Any) {
        LibWrite("Font: no Assets/Fonts/*.FNT, using built-in\n");
    }
    return Any ? 0 : -1;
}

int FontIdByName(const char *Name) {
    UINT32 i;

    if (!Name || Name[0] == 0) {
        return -1;
    }
    /* 旧 theme/DB 全称 → 短名 */
    if (NameEqCi(Name, "AR PL UMing CN")) {
        Name = "UMing";
    } else if (NameEqCi(Name, "Droid Sans Fallback")) {
        Name = "Droid";
    } else if (NameEqCi(Name, "Terminus 10x18") || NameEqCi(Name, "TER10X18")) {
        Name = "Terminus";
    }
    for (i = 0; i < gFontCount; i++) {
        if (gFonts[i] && NameEqCi(Name, gFonts[i]->Name)) {
            return (int)i;
        }
    }
    return -1;
}

int FontReloadAssets(void) {
    UINT32 Cur = gCurrentId;

    (void)FontLoadAssets();
    /* 原 id 若是已卸 runtime：回到内建 Terminus 10x18 */
    if (FontSetById(Cur) != 0 && FontCount() > 0) {
        (void)FontSetById(0);
    }
    return 0;
}

int FontLoadPath(const char *Path) {
    UINT32 i;

    if (!Path || !Path[0]) {
        return -1;
    }
    if (gRuntime[FONT_APP_SLOT].Used) {
        FreeRuntimeSlot(&gRuntime[FONT_APP_SLOT]);
        RebuildFontTable();
    }
    if (TryLoadPath(Path, &gRuntime[FONT_APP_SLOT]) != 0) {
        return -1;
    }
    /* 私有槽允许与系统同名；不跑 FaceNameRegistered */
    RebuildFontTable();
    for (i = 0; i < gFontCount; i++) {
        if (gFonts[i] == &gRuntime[FONT_APP_SLOT].Face) {
            return (int)i;
        }
    }
    FreeRuntimeSlot(&gRuntime[FONT_APP_SLOT]);
    RebuildFontTable();
    return -1;
}

void FontUnloadApp(void) {
    if (!gRuntime[FONT_APP_SLOT].Used) {
        return;
    }
    FreeRuntimeSlot(&gRuntime[FONT_APP_SLOT]);
    RebuildFontTable();
    if (gCurrentId >= gFontCount) {
        gCurrentId = gFontCount ? (gFontCount - 1u) : 0u;
    }
}

UINT32 FontCount(void) {
    return gFontCount;
}

UINT32 FontCurrentId(void) {
    return gCurrentId;
}

const FONT_FACE *FontGetById(UINT32 Id) {
    if (Id >= gFontCount) {
        return 0;
    }
    return gFonts[Id];
}

const FONT_FACE *FontGetCurrent(void) {
    const FONT_FACE *F = FontGetById(gCurrentId);
    if (F == 0) {
        return &gFontFaceTerminus10x18;
    }
    return F;
}

int FontSetById(UINT32 Id) {
    const FONT_FACE *F;
    UINT32 NextSlot;

    if (Id >= gFontCount) {
        return -1;
    }
    F = gFonts[Id];
    NextSlot = gUiTtfSlot;
    if (F == &gFaceUiUming) {
        NextSlot = 0;
    } else if (F == &gFaceUiDroid) {
        NextSlot = 1;
    }
    /* Latin 项不改槽；切 UMing↔Droid 丢全部 HIT（含拉丁）以便换脸 */
    if (NextSlot != gUiTtfSlot) {
        gUiTtfSlot = NextSlot;
        FontTtfCacheDropHits();
    }
    gCurrentId = Id;
    return 0;
}

static UINT32 ScaleOf(const FONT_FACE *F) {
    return F->Scale ? F->Scale : 1u;
}

/*
 * Sun 8×16 的步进对齐 Terminus 的 10 宽。高度拉到汉字后不再横向取样，
 * 否则 1 会比 9、2 粗。只在两侧各留 1 像素，字距稍松。
 */
static UINT32 FaceDrawW(const FONT_FACE *F) {
    UINT32 S = ScaleOf(F);
    UINT32 Base;
    UINT32 NatH;
    UINT32 CellH;

    if (F->Width > 0u && F->Width <= 8u && F->Height > 0u && F->Height <= 16u) {
        Base = 10u * S;
    } else {
        Base = F->Width * S;
    }
    NatH = F->Height * S;
    CellH = FontTtfPixel();
    if (NatH == 0u || CellH <= NatH) {
        return Base;
    }
    return Base + 2u;
}

UINT32 FontCellW(void) {
    return FaceDrawW(FontFaceForLatin());
}

UINT32 FontCellH(void) {
    return FontTtfPixel();
}

UINT32 FontAdvanceX(void) {
    const FONT_FACE *F = FontFaceForLatin();
    UINT32 S = ScaleOf(F);
    UINT32 Mono;

    Mono = FontTtfMonoAdvance();
    if (FontTtfMonoActive() && Mono != 0u) {
        return Mono;
    }
    return FaceDrawW(F) + F->CharSpacing * S;
}

UINT32 FontAdvanceY(void) {
    const FONT_FACE *F = FontFaceForLatin();
    UINT32 S = ScaleOf(F);

    return FontTtfPixel() + F->LineSpacing * S;
}

const UINT8 *FontGlyph(char C) {
    const FONT_FACE *F = FontFaceForLatin();
    UINT32 U = (UINT32)(UINT8)C;

    if (F == 0 || F->Glyphs == 0 || F->BytesPerGlyph == 0) {
        return 0;
    }
    if (U < F->FirstChar || U >= F->FirstChar + F->GlyphCount) {
        return 0;
    }
    return F->Glyphs + (U - F->FirstChar) * F->BytesPerGlyph;
}

UINTN Utf8Decode(const char *S, UINT32 *OutCp) {
    UINT8 C0;
    UINT32 Cp;
    UINTN Need;
    UINTN i;

    if (!S || !OutCp) {
        return 0;
    }
    C0 = (UINT8)S[0];
    if (C0 == 0) {
        return 0;
    }
    if (C0 < 0x80) {
        *OutCp = C0;
        return 1;
    }
    if ((C0 & 0xE0) == 0xC0) {
        Cp = C0 & 0x1F;
        Need = 2;
    } else if ((C0 & 0xF0) == 0xE0) {
        Cp = C0 & 0x0F;
        Need = 3;
    } else if ((C0 & 0xF8) == 0xF0) {
        Cp = C0 & 0x07;
        Need = 4;
    } else {
        return 0;
    }
    for (i = 1; i < Need; i++) {
        UINT8 Cx = (UINT8)S[i];
        if ((Cx & 0xC0) != 0x80) {
            return 0;
        }
        Cp = (Cp << 6) | (UINT32)(Cx & 0x3F);
    }
    *OutCp = Cp;
    return Need;
}

static int gGlyphDefer;

int FontGlyphCpDeferred(void) {
    return gGlyphDefer;
}

const UINT8 *FontGlyphCp(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {
    const FONT_FACE *F;
    const UINT8 *G;
    int UiLatin;

    gGlyphDefer = 0;
    gGlyphCrispLatin = 0;
    /* MONO.TTF：仅 Begin 激活时；壳现默认不激活 */
    if (FontTtfMonoActive() && Cp >= 32u && Cp < 127u) {
        G = FontTtfMonoCacheGet(Cp, OutW, OutH);
        if (G) {
            gGlyphCpBpp = 8;
            gGlyphCrispLatin = 1;
            return G;
        }
    }
    /*
     * cjk-5：选中 UI TTF 时 ASCII 与汉字同文件（UMING/DROID）。
     * 绘制层对拉丁走软 AA（FontGlyphCrispLatin），勿再 GlyphCrisp 硬阈值滤空心。
     * 选 Terminus/Sun：ASCII 仍点阵。
     */
    UiLatin = FontUiTtfSelected() && Cp >= 32u && Cp < 127u;
    if (UiLatin && FontTtfReady() && !FileSystemBootMediumGone()) {
        G = FontTtfCacheGet(Cp, OutW, OutH);
        if (G) {
            gGlyphCpBpp = 8;
            gGlyphCrispLatin = 1;
            return G;
        }
        if (!FontTtfCacheMissed(Cp)) {
            (void)FontTtfCachePending(Cp);
            gGlyphDefer = 1;
            return 0;
        }
        /* 真 MISS：软退点阵，避免空白 */
    }
    if (Cp >= 32u && Cp < 127u) {
        F = FontFaceForLatin();
        G = FontGlyph((char)Cp);
        if (G && F) {
            if (OutW) {
                *OutW = F->Width;
            }
            if (OutH) {
                *OutH = F->Height;
            }
            gGlyphCpBpp = 1;
            return G;
        }
        if (UiLatin) {
            return 0;
        }
    }
    /*
     * 汉字（gui-cjk-ttf / cjk-2）：只走 TTF 缓存。禁止运行时 TOYF/FNT 与
     * CJK32「先点阵后换皮」。未 HIT：Want 排队 + Deferred（本帧不画）；
     * Worker WantDrain + TakePaint 后再出矢量。永久 MISS / 无 TTF → 交 Glyph
     * 缺字框（cjk-3）。
     */
    G = FontTtfCacheGet(Cp, OutW, OutH);
    if (G) {
        gGlyphCpBpp = 8;
        return G;
    }
    if (Cp >= 128u && FontTtfReady() && !FontTtfCacheMissed(Cp) &&
        !FileSystemBootMediumGone()) {
        (void)FontTtfCachePending(Cp);
        gGlyphDefer = 1;
    }
    return 0;
}

UINT32 FontGlyphCpBpp(void) {
    return gGlyphCpBpp ? gGlyphCpBpp : FontCjkBitsPerPixel();
}

UINT32 FontGlyphStretch(UINT32 GlyphH) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 FaceScale = (F && F->Scale) ? F->Scale : 1u;

    (void)GlyphH;
    /*
     * PR-UI-cjk：只尊重 Face->Scale，禁止 CellH/GlyphH 整数拉高。
     * 旧 PR-T1 把 CJK16×2→32 行高，锯齿被放大，NUC 上明显糊。
     * 矮于行高时由 VideoDrawBitmapAt 垂直居中。
     */
    return FaceScale;
}

void FontGlyphBox(UINT32 GlyphW, UINT32 GlyphH, UINT32 *OutW, UINT32 *OutH) {
    UINT32 Scale = FontGlyphStretch(GlyphH);
    UINT32 W;
    UINT32 H;
    UINT32 CellH;

    if (Scale < 1u) {
        Scale = 1u;
    }
    W = GlyphW * Scale;
    H = GlyphH * Scale;
    CellH = FontCellH();
    /* 点阵 18×18 先顶上时，放到和 TTF 一样大，换上真字不再跳一档。 */
    if (GlyphW == GlyphH && GlyphH == FontCjkDim() && GlyphH > 0u &&
        GlyphH < FontTtfPixel()) {
        W = FontTtfPixel();
        H = FontTtfPixel();
    }
    /* 汉字按 FontTtfPixel 原样画。缩回拉丁行高会把上横收成一行。 */
    if (H > CellH && H > 0u && !(W == FontTtfPixel() && H == FontTtfPixel())) {
        W = (UINT32)(((UINT64)W * (UINT64)CellH) / (UINT64)H);
        if (W == 0u) {
            W = 1u;
        }
        H = CellH;
    }
    if (OutW) {
        *OutW = W;
    }
    if (OutH) {
        *OutH = H;
    }
}

UINT32 FontCodepointAdvance(UINT32 Cp) {
    UINT32 W;
    UINT32 H;
    UINT32 Adv;

    if (FontTtfMonoActive() && Cp >= 32u && Cp < 127u) {
        Adv = FontTtfMonoAdvance();
        if (Adv != 0u) {
            return Adv;
        }
    }
    /* cjk-5：UI TTF 拉丁用缓存步进；未 HIT 先半格占位，勿 Terminus 宽跳排 */
    if (FontUiTtfSelected() && Cp >= 32u && Cp < 127u) {
        Adv = FontTtfCacheAdvance(Cp);
        if (Adv != 0u) {
            return Adv;
        }
        Adv = FontTtfPixel() / 2u;
        if (Adv < 1u) {
            Adv = 1u;
        }
        return Adv;
    }
    if (Cp >= 32u && Cp < 127u) {
        Adv = FontAdvanceX();
        if (Adv != 0u) {
            return Adv;
        }
    }
    Adv = FontTtfCacheAdvance(Cp);
    if (Adv != 0u) {
        return Adv;
    }
    if (!FontGlyphCp(Cp, &W, &H)) {
        /* 中文 defer：按 TTF 格占位，勿用 CJK32 宽导致换皮后跳排 */
        W = (Cp >= 128u) ? FontTtfPixel() : FontCjkDim();
        if (W == 0u) {
            W = FontCjkDim();
        }
        H = W;
    }
    FontGlyphBox(W, H, &W, &H);
    return W;
}

UINT32 FontStringWidth(const char *S) {
    UINT32 Total = 0;

    while (S && *S) {
        UINT32 Cp;
        UINTN N;

        if (*S == '\n') {
            break;
        }
        N = Utf8Decode(S, &Cp);
        if (N == 0) {
            S++;
            continue;
        }
        Total += FontCodepointAdvance(Cp);
        S += N;
    }
    return Total;
}
