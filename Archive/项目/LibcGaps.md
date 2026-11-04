# ISO C / libc 缺口表

> **方言**：实现 **C99**（`-std=c99 -pedantic`）。  
> **目标**：**Blocks Hosted 子集**（非 Linux；**不做 POSIX 双轨兼容**）。  
> **树**：  
> - ISO → [`Kernel/User/Library/Libc/`](../../Kernel/User/Library/Libc/)  
> - 自有门面 → [`Kernel/User/Library/Blocks/`](../../Kernel/User/Library/Blocks/)（`start/` + `host/` `dir/` `thread/` …）  
> - 头 → [`Kernel/User/Include/`](../../Kernel/User/Include/)  
> **CRT**：**1.9.6**。活档 [`libc加厚.md`](libc加厚.md)。

---

## 目录分界

| 树 | 内容 |
| --- | --- |
| `Library/Libc/` | 仅 ISO C 实现：`errno` `math` `setjmp` `signal` `stdio` `stdlib` `string` `time` |
| `Library/Blocks/` | **Blocks 自有**：`start/` · `host/` · `dir/` · `thread/` · `socket/` · `stat/` |
| `User/Include/` | 仅子目录：`Libc/`（ISO）· `Blocks/` · `System/` · `Host/` · `Graphics/` · `Ui/` · `Net/` · `VolumePath/`（**不是**对标 Linux） |

链接：`crt0.o` + `syscall.o` + `libc.a` + `libblocks.a`。

---

## C99 Hosted 全头矩阵（拆 PR 用）

状态：`✅实现` / `✅仅头` / `⚠缩水` / `⛔不做` / `☐可选加厚`。

### Freestanding 七头

| 头 | 状态 | 位置 | PR 柱 |
| --- | --- | --- | --- |
| `<float.h>` | ✅仅头 | `include/` | A |
| `<iso646.h>` | ✅仅头 | `include/` | A |
| `<limits.h>` | ✅仅头 | `include/` | A |
| `<stdarg.h>` | ✅仅头 | `include/` | A |
| `<stdbool.h>` | ✅仅头 | `include/` | A |
| `<stddef.h>` | ✅仅头 | `include/` | A |
| `<stdint.h>` | ✅仅头 | `include/` | A |

### Hosted 其余头

| 头 | 状态 | 说明 | PR 柱 |
| --- | --- | --- | --- |
| `<assert.h>` | ✅仅头/薄 | 宏为主 | A |
| `<ctype.h>` | ✅仅头 | 多 inline | A |
| `<errno.h>` | ✅实现 | `Libc/errno/` | B |
| `<math.h>` | ⚠缩水 | soft-float | B |
| `<setjmp.h>` | ⚠缩水 | aarch64 未存浮点 | B |
| `<signal.h>` | ⚠缩水 | 教学子集 | B |
| `<stdio.h>` | ⚠缩水 | 见缩水表 | B |
| `<stdlib.h>` | ⚠缩水 | malloc/qsort 等 | B |
| `<string.h>` | ⚠缩水 | NULL 软化等 | B |
| `<time.h>` | ⚠缩水 | 无 TZ | B |
| `<inttypes.h>` | ☐可选 | 薄宏头 | C |
| `<fenv.h>` | ⛔不做 | 无浮点环境模型 | D |
| `<locale.h>` | ⛔不做 | 本地化；与无 TZ 冲突 | D |
| `<complex.h>` / `<tgmath.h>` | ⛔不做 | 非课设主线 | D |
| `<wchar.h>` / `<wctype.h>` | ⛔不做 | 宽字符双轨；按字节串 | D |

### ⛔ 明确不做

| 不做 | 原因 |
| --- | --- |
| `wchar` / `wctype` | 宽字符体积大；不做 i18n 产品面 |
| `locale` | 本地化全库挂钩；已钉 UTC |
| `complex` / `tgmath` / `fenv` | 数值/环境非主线 |
| **POSIX / Linux 双轨课设** | 废；用户态只认 Blocks 门面 + ISO 子集 |
| 完整 ISO Hosted 全库 | 非 glibc 对标 |

### 非 ISO（柱 E · 勿混进 C99 Hosted PR）

| 目录 | 说明 |
| --- | --- |
| `Blocks/start/` | 进程入口 |
| `Blocks/host/` | File/Process/cwd/sleep/`brk`/映射等 |
| `Blocks/dir/` | `Dir*` |
| `Blocks/thread/` | 线程与协作调度 |
| `Blocks/socket/` · `stat/` | 套接字 / 状态薄封装 |

---

## Blocks Hosted 主列（课用验收）

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

## 表外加厚（已收）

| 项 | 状态 |
| --- | --- |
| virt setjmp（aarch64/riscv） | ✅ |
| exec 继承 environ | ✅ |
| scanf / math 加厚 | ✅ |
| fopen `"w"` 截断 · malloc freelist · clearerr/rewind | ✅ |
| memchr / span · localtime/mktime/difftime | ✅ |

## 已知缩水（诚实课用 · B 柱可选）

| 项 | 说明 |
| --- | --- |
| malloc | 不合并邻块、不向 `brk` 收缩 |
| localtime | `≡ gmtime`（UTC） |
| 静缓 | gmtime/asctime/strtok/environ 非可重入 |
| printf→stdout | ~512 截断 |
| qsort | 插入排序 |
| soft float | 无 `asin`/`isnan` 等 |
| scanf/printf | 无完整 flags / 字段宽 |
| aarch64 setjmp | 未存浮点 |
| FAT `"w"` | unlink 模拟 truncate |
| `fflush(NULL)` | 不刷全部流 |
| 字符串 NULL | 软化（C99 为 UB） |
| `mktime` | 无完整字段归一 |
| `FILE` | `toy_file` 名在头可见 |

---

## 建议 PR 拆分

| 柱 | 范围 |
| --- | --- |
| **A** | 全头矩阵文档勾 / freestanding |
| **B** | `Libc/<头>/` 缩水项（一刀一项） |
| **C** | `inttypes` 等可选薄头（点名） |
| **D** | ⛔ 只文档不实现 |
| **E** | `Library/Blocks/`（与 C99 分列） |

> Review：[`Archive/过程/审查/Blocks-Hosted-libc-CodeReview-1105.md`](../过程/审查/Blocks-Hosted-libc-CodeReview-1105.md)
