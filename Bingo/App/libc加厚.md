# libc 加厚

> **本柱活档**：只认本文首 ★（勿跟 1104 Duo 刀）。  
> **定名**：底库 **`libc.a`**（`-lc`）；废 `libBlocks.a`。旁系仍 `libUi` / `libGraphics` / `libNet` / `libFsUtil`。  
> **口径**：ISO C99 原名；非 ISO 走 Blocks（`File*` / `Process*` / …）。缺口真源 [`LibcGaps.md`](LibcGaps.md)。  
> **口令**：JX / KG / TG（本柱）；不 push 除非另说 TS。

---

## ★ 当前与排队

> **当前 ★**：**PR1 `libc-headers`** — 钉齐 `stdbool.h` / `stdint.h`；审 `ctype.h`。  
> **本刀**：开干。  
> **排队**：PR2 string → PR3 stdlib-parse → … → PR9 atexit。  
> **刚 TG**：PR0 `libc-rename`（`Libc/libc.a`；SDK/Template/LibcDemo 绿）。

| # | PR | 范围 | 验收 | 状态 |
|---|-----|------|------|------|
| 0 | `libc-rename` | `libBlocks.a`→`libc.a`；`User/Library/Libc/libc.a`；Make/SDK/`Bingo` 对齐 | SDK 链 Hello；旧名无链接残留 | ✅ |
| 1 | `libc-headers` | 钉齐 `stdbool.h` / `stdint.h`；审 `ctype.h` | 头可单编；范例无回归 | ★ |
| 2 | `libc-string` | `strerror` / `strtok` / `strrchr` / `strncat`（`memmove` 已有则勾表） | LibcGaps P1 string；`LibcDemo` 绿 | 排队 |
| 3 | `libc-stdlib-parse` | `strtol` / `strtoul` / `strtod`；`getenv`/`setenv` 可后置 | 解析用例；无假 POSIX | 排队 |
| 4 | `libc-stdio-fmt` | `fprintf` / `vfprintf` / `fputs` / `fputc` | 写 FILE* / 串口路径绿 | 排队 |
| 5 | `libc-stdio-buf` | 行/全缓冲 + `fflush` / `ungetc`；宽字符流**不做** | 旧范例不破 | 排队 |
| 6 | `libc-setjmp` | `setjmp.h` + arch 汇编（x64 先） | 跳转烟测 ELF | 排队 |
| 7 | `libc-time` | 薄 `time.h`：`clock`/`time` 垫 `clock_ms` | 头+符号；不承诺完整 TZ | 排队 |
| 8 | `libc-math` | 裁剪 `math.h`；可再拆 | 子集表进 LibcGaps | 排队 |
| 9 | `libc-atexit` | `atexit` / `abort`；完整 `signal` **不**默认开 | 退出钩子烟测 | 排队 |

**不做（本柱）**：`locale`/`wchar`；musl；假 POSIX 公开脸；对齐 Linux syscall 号。  
**自举门槛**：PR0～PR6（P0/P1）收口后再谈 Guest 内编译（sh-6）。

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0 `libc-rename` | ✅ | `User/Library/Libc/libc.a`；废 `libBlocks`；Make/SDK/Bingo 对齐 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR0；★→PR1 `libc-headers`；JX 开干 |
| 2026-11-04 | PR0 编过：产物 `Libc/libc.a`；SDK/Template/LibcDemo 绿 |
| 2026-11-04 | 自 1104 待点名迁出（1104 只撤本柱段）；拆 PR0～9；★→`libc-rename` |
