/*
 * FontTtfCache.c — 64×64×8bpp 定长缓存
 *
 * font-unified-ttf：可打印 ASCII 与汉字同槽；绘制只 Lookup。
 * 栅格仅 Preheat / Want 泵（Worker / 开窗短 burst）。
 * PR-23E：边长按物理像素（FontTtfCell：100→24、150→36、200→48），槽位 64×64。
 */
#include "Font.h"
#include "Hal.h"
#include "Compiler.h"

#define TTF_CACHE_N 1024u
#define TTF_PROBE   32u
#define TTF_CELL    64u
#define TTF_PIX     (TTF_CELL * TTF_CELL)
/* PR-23E：200% 的 ASCII 提权 em 是 60，槽必须装得下（与栅格侧同断言） */
BLOCKS_STATIC_ASSERT(TTF_CELL >= 60u, "TTF_CELL must hold 200% ASCII em 60");
#define TTF_EMPTY   0
#define TTF_HIT     1
#define TTF_MISS    2
#define TTF_WANT_N  128u

typedef struct {
    UINT32 Cp;
    UINT32 Side; /* 位图边长（物理像素，= FontTtfCell）；步进见下一行 */
    UINT32 Adv;  /* 逻辑步进：拉丁用字宽，汉字用 Side/绘制倍数 */
    int State;
    UINT8 Pix[TTF_PIX];
} TTF_SLOT;

static int TtfCpOk(UINT32 Cp) {
    return (Cp >= 32u && Cp != 127u) ? 1 : 0;
}

/*
 * PR-23b：64×64 槽 ×1024 ≈ 4MB。必须上抬 .bss.high（链接在 32MiB），
 * 不能留在低端 .bss：低端段变大后整段 [0x352000, ...) 会跨过 OVMF
 * ~8MiB 保留区，Boot 的 AllocateAddress 整段申请直接失败（实测启动挂死）。
 */
static TTF_SLOT gSlot[TTF_CACHE_N]
    BLOCKS_SECTION(".bss.high") BLOCKS_ALIGNED(64);
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
    TTF_SLOT *Miss = 0;

    for (k = 0; k < TTF_PROBE; k++) {
        TTF_SLOT *S = &gSlot[(i0 + k) % TTF_CACHE_N];

        if (S->State != TTF_EMPTY && S->Cp == Cp) {
            return S;
        }
        if (S->State == TTF_EMPTY && !Empty) {
            Empty = S;
        }
        if (S->State == TTF_MISS && !Miss) {
            Miss = S;
        }
    }
    if (Empty) {
        return Empty;
    }
    if (Miss) {
        return Miss;
    }
    /* 探测窗尽是别人 HIT：全表找空/MISS，勿默默顶掉无关字（曾丢「桌」） */
    for (k = 0; k < TTF_CACHE_N; k++) {
        if (gSlot[k].State == TTF_EMPTY) {
            return &gSlot[k];
        }
    }
    for (k = 0; k < TTF_CACHE_N; k++) {
        if (gSlot[k].State == TTF_MISS) {
            return &gSlot[k];
        }
    }
    /* 真满：覆盖主槽 */
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

void FontTtfCacheDropHits(void) {
    UINT32 i;

    for (i = 0; i < TTF_CACHE_N; i++) {
        if (gSlot[i].State == TTF_HIT) {
            gSlot[i].State = TTF_EMPTY;
            gSlot[i].Cp = 0;
            gSlot[i].Side = 0;
            gSlot[i].Adv = 0;
        }
    }
    gWantN = 0;
    gPaintGen++;
}

void FontTtfCacheDropCjkHits(void) {
    UINT32 i;

    for (i = 0; i < TTF_CACHE_N; i++) {
        if (gSlot[i].State == TTF_HIT && gSlot[i].Cp >= 128u) {
            gSlot[i].State = TTF_EMPTY;
            gSlot[i].Cp = 0;
            gSlot[i].Side = 0;
            gSlot[i].Adv = 0;
        }
    }
    /* 不抬 Want 满；由绘制路径 CacheGet→WantPush 自然补 */
    gPaintGen++;
}

void FontTtfCacheDropMisses(void) {
    UINT32 i;

    for (i = 0; i < TTF_CACHE_N; i++) {
        if (gSlot[i].State == TTF_MISS) {
            gSlot[i].State = TTF_EMPTY;
            gSlot[i].Cp = 0;
            gSlot[i].Side = 0;
            gSlot[i].Adv = 0;
        }
    }
}

/* 比例切换时就地重栅（定义见下）：CacheGet 需要提前声明 */
static int FillSlot(UINT32 Cp, int NotifyPaint);

const UINT8 *FontTtfCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {
    TTF_SLOT *S;

    if (!TtfCpOk(Cp)) {
        return 0;
    }
    S = SlotLookup(Cp);
    if (S) {
        if (S->State == TTF_MISS) {
            /*
             * 真缺字勿再 Want：否则 Drain→Bump→ThemeScene 死循环叠半透栏
             * （开 Settings 等字多的窗时栏很抖）。Init 前灌的 MISS 由
             * FontTtfCacheDropMisses（FontTtfInit 后）清掉再画。
             */
            return 0;
        }
        if (S->Side != FontTtfCell()) {
            /*
             * PR-23E 修：比例/字号切换后槽内是旧边长的位图。旧逻辑只
             * Want 排队等 Worker：界面先按「不画」合成 → 屏上留着旧字
             * 像素，Worker 又被鼠标活动挡着不泵 → 半新半旧（乱码/缺字）。
             * 这里当场按新边长重栅这一格（每个可见字只一次），绘制与
             * 步进立刻同档，不再出现新旧混排。
             */
            (void)FillSlot(Cp, 1);
            S = SlotLookup(Cp);
            if (!S || S->State != TTF_HIT || S->Side != FontTtfCell()) {
                return 0;
            }
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
    if (!S || S->State != TTF_HIT || S->Side != FontTtfCell()) {
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
 * NotifyPaint=1：屏上可能仍是 defer 空白，Gui 须再合成（Want 泵出 TTF）。
 * Preheat 填缓存勿 Notify：否则每字整屏 Refresh → 空桌面偶尔顿一下。
 * 返回 1=本调用新成 HIT（供 WantDrain 决定是否 Bump）。
 */
static int FillSlot(UINT32 Cp, int NotifyPaint) {
    TTF_SLOT *Slot;
    UINT32 Side;

    if (!TtfCpOk(Cp)) {
        return 0;
    }
    Slot = SlotAlloc(Cp);
    /* PR-23E：位图边长 = FontTtfPixel() × Num/Den（100→24、150→36、200→48） */
    Side = FontTtfCell();
    /*
     * 仅 HIT 且边长一致才跳过。MISS 须可重试：
     * SESSION 在 FS/Gui 前时 Console→LocaleTtfPreheat 会在 gInit=0 时灌满 MISS，
     * 若此处把 MISS 当「已填」则 Gui 二次预热永不再栅格（汉字一直缺字框）。
     */
    if (Slot->State == TTF_HIT && Slot->Cp == Cp && Slot->Side == Side) {
        return 0;
    }
    if (FontTtfRasterCp(Cp, Slot->Pix) != 0) {
        Slot->Cp = Cp;
        Slot->Side = Side;
        Slot->Adv = Side;
        Slot->State = TTF_MISS;
        return 0;
    }
    Slot->Cp = Cp;
    Slot->Side = Side;
    /* FT 有真实 advance 则用之；否则拉丁半格、汉字整格 */
    Slot->Adv = FontFtLastAdvance();
    if (Slot->Adv < 1u) {
        Slot->Adv = (Cp < 128u) ? (Side / 2u) : Side;
    }
    /*
     * PR-23E：栅格与 FT 步进都是物理像素，Slot->Adv 必须存逻辑步进
     * （FontCodepointAdvance / 布局直接拿它排字），否则缩放档字距翻倍。
     * 反算按有理数比例：逻辑 = 物理 × Den / Num（150% 下 27 物理 → 18 逻辑；
     * 拿整数 2 去除会得 13，字距立刻缩水）。
     */
    {
        UINT32 Num = HalVideoGetDrawNum();
        UINT32 Den = HalVideoGetDrawDen();

        if (Num != 1u || Den != 1u) {
            if (Num == 0u) {
                Num = 1u;
            }
            Slot->Adv = (UINT32)(((UINT64)Slot->Adv * (UINT64)Den) /
                                 (UINT64)Num);
        }
    }
    if (Slot->Adv < 1u) {
        Slot->Adv = 1u;
    }
    /*
     * PR-23E 修：Adv 是逻辑步进，上限只能是逻辑字格 FontTtfPixel()。
     * 旧代码夹到 Side（物理边长）：50% 汉字 FT 阶段 24、Side=12 →
     * 步进被砍成 12，整行汉字叠一半；150%/200% 恰好没触发。
     * 100%（Side=24）、200%（Side=48）下上限不变，基线 0 差异。
     */
    if (Slot->Adv > FontTtfPixel()) {
        Slot->Adv = FontTtfPixel();
    }
    Slot->State = TTF_HIT;
    if (NotifyPaint) {
        gPaintGen++;
    }
    return 1;
}

/*
 * UI 拉丁 ASCII：同步灌格。汉字仍走 Want/Deferred（防挡鼠）。
 * 若 ASCII 也 defer，首帧会留下「ello」「locks>」，且 TakePaint 狂刷任务栏。
 */
void FontTtfCacheEnsure(UINT32 Cp) {
    if (!TtfCpOk(Cp) || Cp >= 128u) {
        return;
    }
    (void)FillSlot(Cp, 0);
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

        /* 多行客户区（TaskMgr 表）须整段灌；勿在首个 \\n 停掉 */
        if (*S == '\n' || *S == '\r' || *S == '\t') {
            S++;
            continue;
        }
        N = Utf8Decode(S, &Cp);
        if (!N) {
            break;
        }
        if (TtfCpOk(Cp)) {
            (void)FillSlot(Cp, 0);
        }
        S += N;
    }
}

UINT32 FontTtfWantCount(void) {
    return gWantN;
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
    /* NotifyPaint=0：整批 WantDrain 末尾统一 BumpPaint */
    (void)FillSlot(Cp, 0);
    return gWantN > 0 ? 1 : 0;
}

UINT32 FontTtfWantDrain(UINT32 Max) {
    UINT32 N = 0;
    UINT32 Hits = 0;

    if (Max == 0) {
        Max = 32u;
    }
    while (N < Max && gWantN > 0) {
        UINT32 Cp = gWant[0];

        gWantN--;
        {
            UINT32 w;
            for (w = 0; w < gWantN; w++) {
                gWant[w] = gWant[w + 1];
            }
        }
        if (FillSlot(Cp, 0)) {
            Hits++;
        }
        N++;
    }
    /*
     * 队列抽干且本批确有新 HIT 才 Bump。纯 MISS 抽干勿 Bump——否则
     * 开窗 ThemeScene 再画缺字 → Want→Drain→Bump 死循环抖半透栏。
     */
    if (Hits > 0u && gWantN == 0u) {
        FontTtfBumpPaint();
    }
    return N;
}

/* font-latin-mono：ASCII 独立槽，避免与 UI UMing 缓存撞车 */
#define MONO_N 96u

/* mono 槽同样按物理边长；.bss.high 理由同上（96×4096≈384KB） */
static UINT8 gMonoPix[MONO_N][TTF_PIX]
    BLOCKS_SECTION(".bss.high") BLOCKS_ALIGNED(64);
static UINT8 gMonoHit[MONO_N];
static UINT32 gMonoSide;
static UINT32 gMonoCellW;
/* 基线布局算法变更时递增，逼缓存失效 */
static UINT32 gMonoLayout = 4u;
static UINT32 gMonoCachedLayout;

const UINT8 *FontTtfMonoCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {
    UINT32 Idx;
    UINT32 Side;
    UINT32 CellW;

    if (!FontTtfMonoActive() || Cp < 32u || Cp >= 127u) {
        return 0;
    }
    Idx = Cp - 32u;
    Side = FontTtfPixel();
    CellW = FontTtfMonoAdvance();
    if (CellW < 1u) {
        CellW = Side / 2u;
    }
    if (gMonoCachedLayout != gMonoLayout || gMonoSide != Side ||
        gMonoCellW != CellW) {
        UINT32 k;

        for (k = 0; k < MONO_N; k++) {
            gMonoHit[k] = TTF_EMPTY;
        }
        gMonoCachedLayout = gMonoLayout;
        gMonoSide = Side;
        gMonoCellW = CellW;
    }
    if (gMonoHit[Idx] == TTF_HIT) {
        if (OutW) {
            *OutW = CellW;
        }
        if (OutH) {
            *OutH = Side;
        }
        return gMonoPix[Idx];
    }
    if (gMonoHit[Idx] == TTF_MISS) {
        return 0;
    }
    if (FontTtfRasterCp(Cp, gMonoPix[Idx]) != 0) {
        gMonoHit[Idx] = (UINT8)TTF_MISS;
        return 0;
    }
    gMonoHit[Idx] = (UINT8)TTF_HIT;
    if (OutW) {
        *OutW = CellW;
    }
    if (OutH) {
        *OutH = Side;
    }
    return gMonoPix[Idx];
}
