# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.3**。

---

## ★ 当前与排队

> **当前 ★**：**X3 `scanf-thicken`** — `scanf`/`fscanf` + 常用转换。  
> **排队**：X4 math 加厚。  
> **刚 TG**：X1 virt setjmp；X2 exec 继承 environ（栈 envp + `BlocksCrtCallMain`）；CRT → **1.9.3**。

| # | 刀 | 状态 |
|---|-----|------|
| H1～H8 | Hosted 主列 | ✅ |
| X1 | virt setjmp | ✅ |
| X2 | exec 继承 environ | ✅ |
| X3 | scanf 加厚 | ★ |
| X4 | math 加厚 | 排队 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | X1～X2 TG；CRT 1.9.3；★→X3 |
| 2026-11-04 | 表外仍缺入队 X1～X4 |
| 2026-11-04 | H6 remove/rename；CRT 1.9.2 |
