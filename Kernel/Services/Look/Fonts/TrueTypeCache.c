/*
 * FontTtfCache.c — 32×32×8bpp 定长缓存
 *
 * font-unified-ttf：可打印 ASCII 与汉字同槽；绘制只 Lookup。
 * 栅格仅 Preheat / Want 泵（Worker / 开窗短 burst）。
 */
#include "Font.h"

#define TTF_CACHE_N 1024u
#define TTF_PROBE   8u
#define TTF_CELL    32u
#define TTF_PIX     (TTF_CELL * TTF_CELL)
#define TTF_EMPTY   0
#define TTF_HIT     1
#define TTF_MISS    2
#define TTF_WANT_N  128u

typedef struct {
    UINT32 Cp;
    UINT32 Side;
    UINT32 Adv; /* 步进：拉丁用字宽，汉字用 Side */
    int State;
    UINT8 Pix[TTF_PIX];
} TTF_SLOT;

static int TtfCpOk(UINT32 Cp) {
    return (Cp >= 32u && Cp != 127u) ? 1 : 0;
}

static TTF_SLOT gSlot[TTF_CACHE_N];
static UINT32 gWant[TTF_WANT_N];
static UINT32 gWantN;
static UINT32 gPaintGen;

static TTF_SLOT *SlotLookup(UINT32 Cp) {
    UINT32 i0 = Cp % TTF_CACHE_N;
    UINT32 k;

    for (k = 0; k < TTF_PROBE; k++) {
        TTF_SLOT *S = &gSlot[(i0 + k) % TTF_CACHE_N];

        if (S->State != TTF_EMPTY && S->Cp == Cp) {
            return S;
        }
    }
    return 0;
}

static TTF_SLOT *SlotAlloc(UINT32 Cp) {
    UINT32 i0 = Cp % TTF_CACHE_N;
    UINT32 k;
    TTF_SLOT *Empty = 0;

    for (k = 0; k < TTF_PROBE; k++) {
        TTF_SLOT *S = &gSlot[(i0 + k) % TTF_CACHE_N];

        if (S->State != TTF_EMPTY && S->Cp == Cp) {
            return S;
        }
        if (S->State == TTF_EMPTY && !Empty) {
            Empty = S;
        }
    }
    if (Empty) {
        return Empty;
    }
    /* 探测窗满：覆盖主槽 */
    return &gSlot[i0];
}

static void WantPush(UINT32 Cp) {
    UINT32 w;

    if (gWantN >= TTF_WANT_N) {
        return;
    }
    for (w = 0; w < gWantN; w++) {
        if (gWant[w] == Cp) {
            return;
        }
    }
    gWant[gWantN++] = Cp;
}

const UINT8 *FontTtfCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {
    TTF_SLOT *S;

    if (!TtfCpOk(Cp)) {
        return 0;
    }
    S = SlotLookup(Cp);
    if (S) {
        if (S->State == TTF_MISS) {
            /* 早先失败（如 TTF 未 Init）→ 再排队，勿永久钉死 */
            WantPush(Cp);
            return 0;
        }
        if (S->Side != FontTtfPixel()) {
            WantPush(Cp);
            return 0;
        }
        /* 位图仍是 Side×Side；步进见 FontTtfCacheAdvance */
        if (OutW) {
            *OutW = S->Side;
        }
        if (OutH) {
            *OutH = S->Side;
        }
        return S->Pix;
    }
    WantPush(Cp);
    return 0;
}

UINT32 FontTtfCacheAdvance(UINT32 Cp) {
    TTF_SLOT *S;

    if (!TtfCpOk(Cp)) {
        return 0;
    }
    S = SlotLookup(Cp);
    if (!S || S->State != TTF_HIT || S->Side != FontTtfPixel()) {
        return 0;
    }
    return S->Adv ? S->Adv : S->Side;
}

int FontTtfCacheMissed(UINT32 Cp) {
    TTF_SLOT *S;

    if (!TtfCpOk(Cp)) {
        return 0;
    }
    S = SlotLookup(Cp);
    return (S && S->State == TTF_MISS) ? 1 : 0;
}

int FontTtfCachePending(UINT32 Cp) {
    UINT32 w;

    if (!TtfCpOk(Cp) || SlotLookup(Cp) != 0) {
        return 0;
    }
    for (w = 0; w < gWantN; w++) {
        if (gWant[w] == Cp) {
            return 1;
        }
    }
    if (gWantN >= TTF_WANT_N) {
        return 0;
    }
    gWant[gWantN++] = Cp;
    return 1;
}

/*
 * NotifyPaint=1：屏上可能仍是点阵，Gui 须再合成（Want 泵）。
 * Preheat 填缓存勿 Notify：否则每字整屏 Refresh → 空桌面偶尔顿一下。
 */
static void FillSlot(UINT32 Cp, int NotifyPaint) {
    TTF_SLOT *Slot;
    UINT32 Side;

    if (!TtfCpOk(Cp)) {
        return;
    }
    Slot = SlotAlloc(Cp);
    Side = FontTtfPixel();
    /*
     * 仅 HIT 且边长一致才跳过。MISS 须可重试：
     * SESSION 在 FS/Gui 前时 Console→LocaleTtfPreheat 会在 gInit=0 时灌满 MISS，
     * 若此处把 MISS 当「已填」则 Gui 二次预热永不再栅格（汉字一直缺字框）。
     */
    if (Slot->State == TTF_HIT && Slot->Cp == Cp && Slot->Side == Side) {
        return;
    }
    if (FontTtfRasterCp(Cp, Slot->Pix) != 0) {
        Slot->Cp = Cp;
        Slot->Side = Side;
        Slot->Adv = Side;
        Slot->State = TTF_MISS;
    } else {
        Slot->Cp = Cp;
        Slot->Side = Side;
        /* 拉丁用半格步进，避免每字母占满汉字方格；汉字仍 Side */
        Slot->Adv = (Cp < 128u) ? (Side / 2u) : Side;
        if (Slot->Adv < 1u) {
            Slot->Adv = 1u;
        }
        Slot->State = TTF_HIT;
        if (NotifyPaint) {
            gPaintGen++;
        }
    }
}

static UINT32 gPaintSeen;

int FontTtfTakePaint(void) {
    return (gPaintGen != gPaintSeen) ? 1 : 0;
}

void FontTtfAckPaint(void) {
    gPaintSeen = gPaintGen;
}

void FontTtfBumpPaint(void) {
    gPaintGen++;
}

void FontTtfPreheatUtf8(const char *S) {
    while (S && *S) {
        UINT32 Cp;
        UINTN N;

        if (*S == '\n') {
            break;
        }
        N = Utf8Decode(S, &Cp);
        if (!N) {
            break;
        }
        if (TtfCpOk(Cp)) {
            FillSlot(Cp, 0);
        }
        S += N;
    }
}

int FontTtfWantStep(void) {
    UINT32 Cp;

    if (gWantN == 0) {
        return 0;
    }
    Cp = gWant[0];
    gWantN--;
    {
        UINT32 w;
        for (w = 0; w < gWantN; w++) {
            gWant[w] = gWant[w + 1];
        }
    }
    /* NotifyPaint=0：勿 bump gen → TickClock 空闲 Compose；屏上点阵等自然重画 */
    FillSlot(Cp, 0);
    return gWantN > 0 ? 1 : 0;
}

UINT32 FontTtfWantDrain(UINT32 Max) {
    UINT32 N = 0;

    if (Max == 0) {
        Max = 32u;
    }
    while (N < Max && gWantN > 0) {
        (void)FontTtfWantStep();
        N++;
    }
    return N;
}
