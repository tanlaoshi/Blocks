# libc 加厚

> **本柱活档**：只认本文首 ★（勿跟 1104 Duo 刀）。  
> **定名**：底库 **`libc.a`**（`-lc`）；废 `libBlocks.a`。旁系仍 `libUi` / `libGraphics` / `libNet` / `libFsUtil`。  
> **口径**：ISO C99 原名；非 ISO 走 Blocks（`File*` / `Process*` / …）。缺口真源 [`LibcGaps.md`](LibcGaps.md)。  
> **口令**：JX / KG / TG（本柱）；不 push 除非另说 TS。  
> **约定**：无需手测的刀 → 编过即 **TG** 并 **JX** 下一刀。

---

## ★ 当前与排队

> **当前 ★**：**PR2 `libc-string`** — `strerror` / `strtok` / `strrchr` / `strncat`。  
> **本刀**：开干。  
> **排队**：PR3 stdlib-parse → … → PR9 atexit。  
> **刚 TG**：PR1 `libc-headers`（`stdbool`/`stdint`/`ctype`；CRT **1.6.0**）。

| # | PR | 范围 | 验收 | 状态 |
|---|-----|------|------|------|
| 0 | `libc-rename` | `libBlocks.a`→`libc.a` | SDK 链 Hello | ✅ |
| 1 | `libc-headers` | `stdbool` / `stdint` / `ctype`；`types` 走 `stdint` | 头可单编；LibcDemo 绿 | ✅ |
| 2 | `libc-string` | `strerror` / `strtok` / `strrchr` / `strncat` | LibcGaps P1 string；`LibcDemo` 绿 | ★ |
| 3 | `libc-stdlib-parse` | `strtol` / `strtoul` / `strtod`；`getenv`/`setenv` 可后置 | 解析用例 | 排队 |
| 4 | `libc-stdio-fmt` | `fprintf` / `vfprintf` / `fputs` / `fputc` | 写 FILE* 绿 | 排队 |
| 5 | `libc-stdio-buf` | 缓冲 + `fflush` / `ungetc`；宽字符**不做** | 旧范例不破 | 排队 |
| 6 | `libc-setjmp` | `setjmp.h` + arch 汇编（x64 先） | 跳转烟测 | 排队 |
| 7 | `libc-time` | 薄 `time.h` | 头+符号 | 排队 |
| 8 | `libc-math` | 裁剪 `math.h` | 子集进 Gaps | 排队 |
| 9 | `libc-atexit` | `atexit` / `abort` | 退出钩子烟测 | 排队 |

**不做（本柱）**：`locale`/`wchar`；musl；假 POSIX 公开脸；对齐 Linux syscall 号。  
**自举门槛**：PR0～PR6（P0/P1）收口后再谈 Guest 内编译（sh-6）。

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0 `libc-rename` | ✅ | `User/Library/Libc/libc.a` |
| PR1 `libc-headers` | ✅ | `stdbool`/`stdint`/`ctype`；CRT 1.6.0 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR1；★→PR2；约定无手测连 TG+JX |
| 2026-11-04 | **TG** PR0；★→PR1 |
| 2026-11-04 | 自 1104 待点名迁出；拆 PR0～9 |
