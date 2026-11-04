# libc 加厚

> **本柱活档**：只认本文首 ★（勿跟 1104 Duo 刀）。  
> **定名**：底库 **`libc.a`**（`-lc`）；废 `libBlocks.a`。旁系仍 `libUi` / `libGraphics` / `libNet` / `libFsUtil`。  
> **口径**：ISO C99 原名；非 ISO 走 Blocks（`File*` / `Process*` / …）。缺口真源 [`LibcGaps.md`](LibcGaps.md)。  
> **口令**：JX / KG / TG（本柱）；不 push 除非另说 TS。  
> **约定**：无需手测的刀 → 编过即 **TG** 并 **JX** 下一刀。

---

## ★ 当前与排队

> **当前 ★**：**PR4 `libc-stdio-fmt`** — `fprintf` / `vfprintf` / `fputs` / `fputc`。  
> **本刀**：开干。  
> **排队**：PR5 stdio-buf → … → PR9 atexit。  
> **刚 TG**：PR3 `libc-stdlib-parse`（`strtol`/`strtoul`/`strtod`；`limits.h`；`ERANGE`）。

| # | PR | 范围 | 验收 | 状态 |
|---|-----|------|------|------|
| 0–2 | rename / headers / string | — | — | ✅ |
| 3 | `libc-stdlib-parse` | `strtol` / `strtoul` / `strtod`；env 后置 | 解析链绿 | ✅ |
| 4 | `libc-stdio-fmt` | `fprintf` / `vfprintf` / `fputs` / `fputc` | 写 FILE* 绿 | ★ |
| 5 | `libc-stdio-buf` | 缓冲 + `fflush` / `ungetc` | 旧范例不破 | 排队 |
| 6 | `libc-setjmp` | `setjmp.h` + arch 汇编（x64 先） | 跳转烟测 | 排队 |
| 7 | `libc-time` | 薄 `time.h` | 头+符号 | 排队 |
| 8 | `libc-math` | 裁剪 `math.h` | 子集进 Gaps | 排队 |
| 9 | `libc-atexit` | `atexit` / `abort` | 退出钩子烟测 | 排队 |

**不做（本柱）**：`locale`/`wchar`；musl；假 POSIX 公开脸；对齐 Linux syscall 号。

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0 `libc-rename` | ✅ | `Libc/libc.a` |
| PR1 `libc-headers` | ✅ | CRT 1.6.0 |
| PR2 `libc-string` | ✅ | `string_extra.c` |
| PR3 `libc-stdlib-parse` | ✅ | `stdlib_parse.c`；`limits.h`；env 后置 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR3；★→PR4 |
| 2026-11-04 | **TG** PR2；★→PR3 |
| 2026-11-04 | **TG** PR1；约定无手测连 TG+JX |
| 2026-11-04 | **TG** PR0 |
