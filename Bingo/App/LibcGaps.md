# ISO C / libc 缺口表

> **方言**：实现 **C99**（`-std=c99 -pedantic`）。  
> **目标**：**Blocks Hosted**（非 Linux 对标）。不做 `wchar`/`locale`/`complex`。  
> **CRT**：**1.9.6**。活档 [`libc加厚.md`](libc加厚.md)。

## Freestanding 七头

全部 ✅。

## Blocks Hosted 主列

| 项 | 状态 |
| --- | --- |
| printf 长度/`%f` | ✅ |
| stdin/out/err | ✅ |
| fgets/sscanf 最小 | ✅ |
| getenv/setenv | ✅ |
| assert/bsearch/rand | ✅ |
| raise/kill | ✅ |
| 墙上钟 | ✅ |
| remove/rename | ✅ |

## 表外加厚

| 项 | 状态 |
| --- | --- |
| virt setjmp（aarch64/riscv） | ✅ |
| exec 继承 environ | ✅ |
| scanf 加厚（scanf/fscanf + `%x/%o/%f/%c/%l*`） | ✅ |
| math 加厚（sin/cos/tan/atan/atan2/exp/log/log10） | ✅ |
| fopen `"w"`/`"w+"` 截断（unlink+open） | ✅ |
| malloc freelist | ✅ |
| clearerr / rewind | ✅ |
| memchr / strspn / strcspn / strpbrk | ✅ |
| localtime / mktime / difftime；strftime `%a/%b/%w` | ✅ |

## 已知缩水（诚实课用，非 P1）

| 项 | 说明 |
| --- | --- |
| malloc | freelist 不合并邻块、不向 `brk` 收缩 |
| localtime | 无 TZ：`≡ gmtime`（UTC） |
| gmtime / asctime / strtok / environ 静缓 | 非可重入 |
| printf 写 stdout | 单缓冲约 512；更长行截断 |
| qsort | 插入排序（教学稳定、大数据慢） |
| soft float | 教学精度；无 `asin`/`isnan` 等扩展 |
| scanf/printf | 无字段宽 / `%*` / 左对齐等完整 flags |
| aarch64 setjmp | 未保存浮点寄存器 |
| FAT | 无内核 truncate；CRT 用 unlink 模拟 `"w"` |
| `fflush(NULL)` | 不刷全部流，直接成功返回 |
| 字符串 NULL | `strlen`/`strcmp` 等软化；严格 C99 为 UB |
| `mktime` | 按 UTC；无完整越界字段归一 |
| `FILE` | 头里 `typedef struct ToyFile FILE`（名泄漏） |

仍不做：`wchar`/`locale`/`complex`、完整 ISO Hosted 全库。

> **Review（1105）**：[`Archive/过程/审查/Blocks-Hosted-libc-CodeReview-1105.md`](../../Archive/过程/审查/Blocks-Hosted-libc-CodeReview-1105.md) — 功能柱可留；实现侧展览全称是课用可读性约定，保留。
