# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.2**（本柱推进中）。

---

## ★ 当前与排队

> **当前 ★**：**X1 `virt-setjmp`** — aarch64 / riscv `setjmp`/`longjmp`（现仅 x86_64）。  
> **排队**：X2 exec 继承 environ → X3 scanf 加厚 → X4 math 加厚。  
> **刚 TG**：H1～H8 Hosted 主列收完；CRT 1.9.2。

| # | 刀 | 状态 |
|---|-----|------|
| H1～H8 | Hosted 主列 | ✅ |
| X1 | virt setjmp | ★ |
| X2 | exec 继承 environ | 排队 |
| X3 | scanf 加厚 | 排队 |
| X4 | math 加厚 | 排队 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | 表外仍缺入队 X1～X4；★→X1 |
| 2026-11-04 | H6：`SYS_FILE_UNLINK`/`RENAME`；CRT 1.9.2；★空 |
| 2026-11-04 | H5：`SYS_TIME_WALL`；CRT 1.9.1 |
| 2026-11-04 | **钉 C99**；H1～H4/H7/H8；CRT 1.9.0 |
