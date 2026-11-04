# Blocks Hosted libc · Code Review（1105 `libc-review`）

> **范围**：CRT **1.9.6**（`Kernel/User/crt` + ISO 头）；对表 `Bingo/App/libc加厚.md` / `LibcGaps.md`；近列 L1～L6 / R1～R6。  
> **命名口径**：公开符号跟 ISO；**实现侧展览全称是课用可读性的刻意选择**（非误套 Kernel 铁律、也非待清债务）。对照 musl 短名只作「业界习惯」备注，**不**据此要求回炉。  
> **日期**：2026-11-04（口径按作者说明修订同日）。

---

## 总判

| 面 | 判 |
| --- | --- |
| 公开 API / 头 | ✅ ISO 名齐；Hosted 课用面可用 |
| L1～L6 功能 | ✅ 与缺口表大体一致；缩水大多诚实 |
| 行数 | ✅ 单文件均 ≤300（最满 `stdio_scan_format.c` ~278） |
| 内部命名（R1～R6） | ✅ **故意**：全称/铺平换课堂可读；公开名仍 ISO |
| 阻塞发版 | 无 P0；CRT 1.9.6 可留 |

**一句话**：功能柱收得住；展览风是可读性策略，与 musl 短名不同源，保留。

---

## 命名

**已遵守**

- 公开：`strlen` / `fopen` / `malloc` 等 ISO 名。
- 头文件声明侧多仍短形参（`fmt`/`s`/`fp`），对学生「调用面」友好。

**展览实现（R 柱，作者定）**

- 形参/局部全称（`String`/`ByteCount`/`Stream`…）、分支铺平、契约注释 — 目标是**读实现**比读 musl 一眼更跟得上。
- 与 Kernel「禁缩写」外观相近，但动机是 **libc 课可读**，不是板债规则下沉。
- `BlocksScan*`：私有扫描引擎前缀；可留。若以后想更像 stdio 私有层，改名属品味，非缺陷。
- `typedef struct ToyFile FILE`：名从公开头可见 — 课用可接受；若嫌漏，可只把结构体挪进 `stdio_private.h`（可选 polish）。

**结论**：不把展览风标成风格债；后续改 CRT **继续**公开 ISO + 实现可读优先，不必强行收成 `s`/`n`。

---

## 功能抽查（L1～L6）

| 刀 | 结论 |
| --- | --- |
| L1 `fopen` `"w"` | unlink 后 `FileOpen`；内核 `SchedulerFdOpen`「缺文件仍开」可创建 — 路径成立。FAT 无 truncate 已写缺口。 |
| L2 malloc | first-fit freelist；不合并/不缩 brk — 与缺口一致。无 magic，双 free 可环 — 课用可接受。 |
| L3 clearerr/rewind | 形对；`rewind` = `fseek`+`clearerr`。 |
| L4 memchr/span | 语义正常；NULL 输入多返回 0（见缺口补记）。 |
| L5 time | `localtime≡gmtime` 注释+缺口诚实；`mktime` 按 UTC、拒绝明显坏月日，不做完整字段归一。 |
| L6 缺口表 | 主缩水在；见「缺口补记」。 |

其它：`printf` 512 截断、aarch64 `setjmp` 无浮点、`environ` 首次改前拷表 — 均与缺口/注释一致。

---

## 缺口补记（已写入 `LibcGaps`）

| 项 | 说明 |
| --- | --- |
| `fflush(NULL)` | 不刷全部流，直接成功返回 |
| 字符串 NULL | `strlen`/`strcmp` 等软化；严格 C99 为 UB |
| `mktime` | 按 UTC；无完整越界字段归一 |
| `FILE` | 头里 `typedef struct ToyFile FILE`（名可见；课用可选藏） |

---

## 红线

- 未链宿主 glibc；CRT 自洽。  
- `-std=c99` 方言档在；GNU `__*` 类型预定义未见散落。  
- 公开符号未抢走 ISO 名。

---

## 收口

1. Review ✅；**不开**命名回炉。  
2. 展览风保留为 Hosted 可读约定。  
3. 功能/缩水问题才开下一刀（点名再说）。
