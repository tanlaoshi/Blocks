# ToyOS 系统开发编年史（本机对话）

> **范围**：本机 `/home/tank/ToyOS` 上，与 Cursor Agent 的接棒对话。  
> **原始记录**：[`agent-transcripts/94534149-8d2c-442c-b0c1-71cca8f5b19d`](../../../../.cursor/projects/home-tank-ToyOS/agent-transcripts/94534149-8d2c-442c-b0c1-71cca8f5b19d/94534149-8d2c-442c-b0c1-71cca8f5b19d.jsonl)（路径相对于本机 Cursor 工程）。  
> **时间跨度**：2026-10-05 19:17 → 2026-10-07 08:29（主体）；本稿整理日 2026-10-27。  
> **说明**：此前 ToyOS 在**另一台电脑**上已做到封版 **0.1.0**；本机从「人民可读 / 代码可读性」接棒。编年史只记**本机这段对话**里问了什么、怎么定、怎么落地；更早刀号见 [`进展-2026-10.md`](../待做/进展-2026-10.md) / 路线图归档。

**协作暗号（本段反复出现）**

| 暗号 | 含义 |
| ---- | ---- |
| **JX** | 按文首 ★ 开刀实现，**不** commit |
| **TG** | 本地 commit，推进 ★；默认**不** push |
| **TS** | push 远程 |
| **TS TAG** | 升版本 + 打 tag +（通常）push |

---

## 〇、接棒前背景（对话外，便于读）

| 项 | 状态 |
| -- | ---- |
| 封版 | ToyOS **0.1.0**（Tag `v0.1.0`）已在旧机收官 |
| 树 | `ToyBoot` / `ToyKernel` / `ToyImage` / `Build` / `EDK2` / `Scripts` |
| 文首 ★ | 空；日常认 `进展-2026-10.md` |
| 痛点（用户开场） | 「代码写得很好，普通人看不懂」→ 要做**人民的操作系统** |

---

## 一、总时间线（一页表）

| 时刻（UTC+8） | 用户意图 | 结果摘要 | 关键提交 / 产物 |
| ------------- | -------- | -------- | ---------------- |
| 10-05 19:17 | 接棒；人民可读 | 立柱规格，不改代码 | `人民可读代码.md` |
| 10-05 19:21～19:25 | 可读性规范（分层+顺序表） | 只写文档；纠正「文档也 ≤300」 | `代码可读性规范.md` 等 |
| 10-05 19:26～21:31 | `PR-K-seq-1…4` | KernelMain 三路分发 → 三表 Run → Common 顺序 Spawn → Init 拆文件 | `af0964c`…`0860932` |
| 10-05 21:30～21:43 | TaskMgr 人话头；菜单没有任务管理器 | 人话头 + 认盘上包 / `si.taskmgr` | `a0faaf7`；Image `1118317` |
| 10-05 21:42～21:52 | ktdemo 改名；TAG 0.1.1；配 SSH 后 TS | `KernelTaskDemo`；`v0.1.1` 推上 GitHub | `8307707` + tag |
| 10-05 23:21～23:34 | READ-2 / READ-3 | 学生调度器、Desktop 人话头；人民可读刀表收官 | `a214fa8` `efe3ce8` |
| 10-05 23:56～10-06 00:07 | 目录树 / 四层驱动方案 | 长草案；采纳 Class→Protocol→Family→Chip | `目录树与可读性方案.md` |
| 10-06 08:24 | 换思路：Boot/Kernel/Runtime + ToyOSNew | 新骨架与新文；旧仓不搬 | `~/ToyOSNew` |
| 10-07 08:28 | TG TS 去另一台同步 | Kernel 推到 `46ed2b6`；ToyOSNew 镜像进文档 | `add7ea3` `46ed2b6` |
| 10-27 01:40 | 写系统开发编年史 | 本文 | 本稿 |

---

## 二、分日详录

### 2026-10-05 · 接棒与「人民可读」开柱

#### 19:17 — 接棒宣言

**问 / 嘱：**  
工作目录是别的电脑用 Cursor 做完的，这里接棒。代码好，但普通人看不懂；要逐步改造，做**人民的操作系统**。

**怎么处理：**  
1. 摸清树：封版 0.1.0、★ 空、日常进展文。  
2. **不**立刻翻全库；先定柱：标识符继续英文 PascalCase；文件头用人话四块；零行为、不破 ABI；一刀一模块。  
3. 写出规格 [`待做/人民可读代码.md`](../待做/人民可读代码.md)（人话 / 从哪读 / 别改 / 想照着做）。

**要点：** 「难读」主因不是英文，而是入口文件像给 Agent 的备忘（刀号密电），缺少说明书。

---

#### 19:21 — 顺序表式主函数（规划）

**问 / 嘱：**  
按「主文件三件事、主函数是顺序表不是策略表、平台显式分路径」规划文档并拆 PR；结合现码。

**怎么处理：**  
对照 `KernelMain` / 模块表现实；准备写成规范。注意教学示意与现码旗标可能不一致。

---

#### 19:22 — 钉死任务：代码可读性规范（只文档）

**问 / 嘱：**  
精确任务清单：新建 `代码可读性规范.md`，更新命名规范 §2.6、接手指南、开发 README、路线图原则 9/10；**不改任何代码、不改 ★**；文档曾要求 ≤300 行。

**怎么处理：**  
1. 落地上述文档。  
2. **关键纠偏：** 示意里若**先**判 `VirtSerialConsole` 再返回 Virt，会把 **QEMU 有 GOP 的桌面**送进无显示路径。现码必须是：  
   - `HalConsoleOnly()` → 串口子集  
   - `HasFrameBuffer && VirtSerialConsole` → virt 桌面  
   - 其余 → Full  
3. 规范正文写清对照现码的分发顺序。

---

#### 19:25 — 文档不受 300 行限制

**问 / 嘱：**  
文档不受 300 行限制，只有代码必须遵守。

**怎么处理：**  
改可读性规范 / 命名规范 / 接手指南 / 路线图原则：行数帽只卡 `.c` 与函数（300/60/30/15）；`.md` 无行数上限。

---

### 2026-10-05 · KernelMain 顺序表四刀（PR-K-seq）

#### 19:26 — JX PR-K-seq-1

**问：** `JX PR-K-seq-1`

**做了什么：**  
`KernelMain` 只留三路分发；抽出 `KernelMainVirt` / `VirtDesktop` / `Full` / `Common`；行为对齐现旗标。未 commit。

**手测：** 用户后来说可进桌面。

---

#### 20:25 — TG（seq-1）

**问：** 「可以进桌面，TG」

**做了什么：**  
提交 `af0964c`。★ 推到 **PR-K-seq-2**。未 push。未跟踪的 `.ini` 未入库。

---

#### 20:29 — JX（seq-2）

**问：** `JX`

**做了什么：**  
去掉 `KernelModulesRun()` 里选表 if；三张表各一个 `Run*`：`RunVirt` / `RunVirtDesktop` / `RunFull`，由路径函数对号入座。x86 构建过。

---

#### 20:32 — TG JX（seq-2 → seq-3）

**问：** `TG JX`

**做了什么：**  
1. TG seq-2 → `a0fa03f`。  
2. JX seq-3：`KernelMainCommon` 变成纯顺序 Spawn/Start；失败停机与 `input` 软退下沉到辅助函数（如 `KernelSpawnOrPark`、`KernelTrySpawnInput`）。未 commit seq-3。

---

#### 21:25 — TG JX（seq-3 → seq-4）

**问：** `TG JX`

**做了什么：**  
1. TG seq-3 → `af9d3ad`。  
2. JX seq-4：`InitializeVideo` / `Usb` / `Gui` 拆到  
   `KernelModulesInitVideo.c` / `Usb.c` / `Gui.c`；表顺序不变。

---

#### 21:30 — TG JX（seq-4 → READ-1）

**问：** `TG JX`

**做了什么：**  
1. TG seq-4 → `0860932`。Kernel 顺序表柱实现刀收官。  
2. JX **PR-READ-1**：`TaskMgr.c` 四块人话头 + 「为什么」注释（List 不写字、与 `ps` 同源、Pid=槽位+1、按钮顺序等）。逻辑未改。

---

### 2026-10-05 · 任务管理器看不见 / 人话样板

#### 21:36 — 「开始菜单没有任务管理器」

**问：** 开始菜单没有任务管理器。

**根因：**  
1. 产品设计上**不在开始顶栏**，而在 **开始 → Apps**（PKG `taskbar=yes`）。  
2. 盘上已有 `Apps/taskmgr/TASKMGR.ELF`，但 `TOYOS.DB` **没有** `si.taskmgr`；`StoreListInstalled` 只认已装清单。

**怎么修：**  
1. `StoreListInstalled`：列已装前先 `StoreAdoptDiskApps`——catalog 有、盘上有 ELF、无 `si.*` 则补登记。  
2. 种子 `ToyImage/RootFs/X64/TOYOS.DB` 加 `si.taskmgr=…`。  
未立刻 TG；等用户确认可见。

---

#### 21:42 — TG + 问 ktdemo + JX 改名

**问：** `TG`；另 **ktdemo 是干什么的？名字上看不出来**；`JX`。

**答 / 做：**  
- **ktdemo**：开机拉起的教学慢任务，进度 25%→100%，演示 `KernelTask` 登记与调度让出；**不是**商店 worker。  
- TG READ-1 + 菜单修复 → Kernel `a0faaf7`；Image `1118317`。  
- JX **PR-READ-name**：任务名改为 `KernelTaskDemo`（Name 最长 15 字符内可读）；日志改为 `KernelTask: Demo Done`。

---

### 2026-10-05 · 封版 0.1.1 与推送

#### 21:45 — TS TAG 0.1.1

**问：** `TS TAG 0.1.1`

**做了什么：**  
1. 把未提交的 `KernelTaskDemo` 与版本串一并入库。  
2. `TOY_OS_VERSION_STRING` → `ToyOS 0.1.1`。  
3. Annotated tag **`v0.1.1`** @ `8307707`。  
4. **首次 TS 失败**：本机无 GitHub SSH key（`Permission denied (publickey)`）。Tag 只在本地。

---

#### 21:52 — 「我添加 key 了」

**问：** 已加 key。

**做了什么：**  
SSH 通；`ToyKernel` `main` + `v0.1.1`、`ToyImage` `master`（si.taskmgr）推上 origin。Image 里大量 ELF/字体脏文件**未**提交。

---

### 2026-10-05 夜 · 人民可读收官

#### 23:21 — JX READ-2

**问：** `JX`

**做了什么：**  
`SchedulerPriority.c` 四块人话头；`如何写一个调度器.md` 路径从已死的 `Common/Modules/` 改到 `CodeC-Modules/`。`runtests.sh scheduler` 两套政策均 ok。

---

#### 23:24 — TG JX（READ-2 → READ-3）

**问：** `TG JX`

**做了什么：**  
1. TG READ-2 → `a214fa8`。  
2. JX READ-3：`Desktop.c` 人话头；`DesktopInitialize` 等处去掉 PR 暗号，改成「为什么」（单核 Worker 优先级导致图标 defer 永不加载等）。

---

#### 23:34 — TG READ-3

**问：** `TG`

**做了什么：**  
`efe3ce8`。人民可读刀表 **READ-0…3 收官**；★ 空。当时本地领先 origin 2 个提交（后于 10-07 一并推）。

---

### 2026-10-05 23:56 ～ 10-06 凌晨 · 大结构严肃讨论

#### 23:56 — 九条目录/可读性需求 → 方案文档

**问（摘要）：**  
刚才做得还差很远。要：  
1. 根目录字母序 = 开发→编译→运行（boot→kernel→build→image），但裸名排不成；  
2. Boot 按架构分；Kernel 按依赖分层，CodeA 不满意；  
3. HAL 按架构，Library 及以后不分；  
4. Library/Core 要用前缀才能让 Core 排后面；  
5～7. 模块夹按执行序；主 `.c`+辅文件；一文件一功能，拆分排序未想好；  
8. Documents/Scripts 统一，位置未定；  
9. 自说明长名。  
先出方案文档，原文放文末；并找外部分案作参考。

**怎么处理：**  
写成 [`目录树与可读性方案.md`](目录树与可读性方案.md)：  
- 讨论默认根级 `10-Boot`…`40-Image`；  
- Kernel `10-Hal`…`50-User` 取代 CodeA；  
- 主名前缀保证 `Kernel.c` 排在 `KernelXxx.c` 前；  
- 附 OSDev/Linux/Fuchsia 等对照；  
- **不改树**。

---

#### 10-06 00:04 — 更模块化 + 四层驱动（DeepSeek）

**问：**  
加硬件型号应归类、加子文件夹，型号不与上层混；与 DeepSeek 讨论过 4 层驱动；其它模块也要换夹即可替换；契约要定好（如 `Hal.h`）。

**怎么处理（第一稿）：**  
在方案 §15 写「契约 / 类 / 总线 / 型号」讨论稿，并对照现网「一设备一夹」、`*_OPS`。请用户核对 DeepSeek 原名。

---

#### 10-06 00:07 — 用户钉死四层命名

**问（定名）：**

```text
Class → Protocol → Family → Chip
Block/Input/Net/Display/Audio
Net → NicL2（现仅以太网）
E1000 / Rtl8169 / Alx / Iwl
82540 / 82574 / I219 …（quirks）
```

**怎么处理：**  
重写 §15：**采纳**该四层；`Hal.h`/Backend 改为**横切契约**；PCI/USB **不是**第二层。待拍板：Chip 建目录还是只做 quirks 表等。

---

### 2026-10-06 上午 · 换思路 ToyOSNew

#### 08:24 — Boot / Kernel / Runtime；Build 回归两侧

**问：**  
换一套思路，重新写文档：  
1. 根下三子目录 **Boot、Kernel、Runtime**（改名）；  
2. **取消顶层 Build**，产物回 Boot/Kernel 各自 Build；  
3. Documents 拆开：根只有 README，大部进 Kernel，少部进 Boot；  
4. **现仓不动**；在 `~/ToyOSNew` 逐步迁移；先文档，确认后再动手。

**怎么处理：**  
1. 建 `~/ToyOSNew/{Boot,Kernel,Runtime}` 空骨架 + README。  
2. 新文 [`目录结构-ToyOSNew.md`](目录结构-ToyOSNew.md)（权威后亦镜像进 Kernel 文档）。  
3. 标明旧「数字前缀顶层 + 顶层 Build」**被取代**；驱动四层等确认大树后再接。  
4. **未搬任何 `.c`。**

---

### 2026-10-07 · 同步到另一台电脑

#### 08:28 — TG TS

**问：** 先 TG TS，到另一台电脑同步。

**怎么处理：**  
1. 提交并推送目录方案文档、进展笔记、ToyOSNew 骨架**镜像**到 `ToyKernel`（因 GitHub 尚无独立 `ToyOSNew` 仓）。  
2. 一并推送此前未推的 READ-2/3。  
3. HEAD **`46ed2b6`**。  
4. 说明：另一台 `git pull` ToyKernel 即可；真树 `~/ToyOSNew` 需本机另建或日后独立仓。

**本段推送相关提交：**

| Hash | 说明 |
| ---- | ---- |
| `add7ea3` | 目录树可读性草案 + ToyOSNew 大结构方案 |
| `46ed2b6` | ToyOSNew 骨架镜像进仓 |

---

### 2026-10-27 · 编年史

#### 01:40 — 整理本机对话为系统开发编年史

**问：**  
要写系统开发编年史；整理本机上咱俩的对话：何时、问了什么、怎么解决；写到文档，可详细点。

**怎么处理：**  
根据 agent transcript + git log 整理为**本文**；挂入开发文档表。

---

## 三、本机落地的代码/版本成果（对照 git）

| 提交 | 主题 |
| ---- | ---- |
| `af0964c` | seq-1 KernelMain 三路分发 |
| `a0fa03f` | seq-2 三张模块表各一 Run |
| `af9d3ad` | seq-3 Common 顺序拉起任务 |
| `0860932` | seq-4 Video/Usb/Gui Init 拆文件 |
| `a0faaf7` | READ-1 TaskMgr 人话头；Apps 认预装包 |
| `1118317`（Image） | 种子 DB `si.taskmgr` |
| `8307707` + **v0.1.1** | KernelTaskDemo；版本串 0.1.1 |
| `a214fa8` | READ-2 学生调度器人话头 |
| `efe3ce8` | READ-3 Desktop 人话头；人民可读收官 |
| `add7ea3` / `46ed2b6` | 目录方案与 ToyOSNew 文档镜像 |

**版本：** `0.1.0`（接棒前）→ **`0.1.1`**（本机 TAG + 推送）。

---

## 四、思想演变（读对话时最有用的一条线）

```text
「人民可读」人话文件头
    → 「顺序表」KernelMain / 模块表（结构可读）
    → 样板：TaskMgr / 学生调度器 / Desktop
    → 发现：菜单/DB 契约也会让「看得见」失败
    → 见字知意：ktdemo → KernelTaskDemo；TAG 0.1.1
    → 仍嫌差很远：目录字母序 = 故事；数字前缀草案
    → 模块化：Class→Protocol→Family→Chip + 横切 Hal.h
    → 换思路：根只要 Boot/Kernel/Runtime；Build 分家；ToyOSNew 迁移
```

**未完成（对话停在确认前）：**  
ToyOSNew 物理迁移未开；Chip 目录 vs quirks、Runtime 定名、EDK2/Scripts 归位等仍在方案 § 待拍板。

---

## 五、关键文档索引

| 文档 | 角色 |
| ---- | ---- |
| [`人民可读代码.md`](../待做/人民可读代码.md) | 人话头模板与 READ 刀表 |
| [`代码可读性规范.md`](代码可读性规范.md) | 分层 + 顺序表 + 长名 |
| [`目录树与可读性方案.md`](目录树与可读性方案.md) | 旧顶层数字前缀草案 + 四层驱动 §15 |
| [`目录结构-ToyOSNew.md`](目录结构-ToyOSNew.md) | **现行大树思路** Boot/Kernel/Runtime |
| [`../待做/ToyOSNew/`](../待做/ToyOSNew/) | 骨架镜像（随 Kernel 仓同步） |
| [`../待做/进展-2026-10.md`](../待做/进展-2026-10.md) | 当月 ★ / 日记 |

---

## 六、修订记录

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-27 | 初稿：据本机 transcript + git log 整理 10-05～10-07 对话 |
