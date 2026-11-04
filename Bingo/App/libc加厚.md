# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.4**。

---

## ★ 当前与排队

> **当前 ★**：（空）  
> **排队**：（空）  
> **刚 TG**：X3 scanf 加厚；X4 math 加厚（trig/exp/log）；CRT → **1.9.4**。  
> （X1 virt setjmp、X2 exec environ 已入树。）

| # | 刀 | 状态 |
|---|-----|------|
| H1～H8 | Hosted 主列 | ✅ |
| X1 | virt setjmp | ✅ |
| X2 | exec 继承 environ | ✅ |
| X3 | scanf 加厚 | ✅ |
| X4 | math 加厚 | ✅ |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | X3～X4 TG；CRT 1.9.4；★空；表外仍缺收完 |
| 2026-11-04 | X1～X2 TG；CRT 1.9.3 |
| 2026-11-04 | H6 remove/rename；CRT 1.9.2 |
