# 17 · Kernel 模块化 —— 设计评审包（v0，待 Cursor 评审）

> 用途：设计稿（Doc 16）已经定了"为什么"，这份定"怎么做"，并且**把可以被推翻的地方摆出来**。
> 评审人只需要看 §6 的 12 个问题 + §5 的风险清单；其余是支撑材料。
> **评审通过前不写代码。**

---

## 1 · 决策记录（ADR，每条都能被推翻）

### ADR-1：`.mod` 用 ELF，加 `.blocks` 段
- **背景**：内核已有 `Core/Elf`（解析 + 重定位，x64/ARM64/RISCV 三套 reloc 已实现并用于用户态）。
- **选项**：A. 复用 ELF；B. 自定义段格式（更小更快，但要自己写重定位）。
- **选择**：A。
- **理由**：重定位是最容易错的一块，已有代码且三架构都跑通过；`.blocks` 段承载元数据（名称/版本/依赖/导出表）。
- **推翻条件**：若 ELF 加载后的体积/重定位时间影响启动（预计 <5 ms，可实测），改走 B。

### ADR-2：模块 ABI = 一张函数指针表 + 版本号，**不做 C++ 名字修饰/结构体直接共享**
- **背景**：驱动与内核必须约定调用方式。
- **选择**：模块导出唯一入口
  ```c
  int BlocksModuleInit(const BLOCKS_API *Api, BLOCKS_DRIVER *Out);
  ```
  内核把 `BLOCKS_API`（内核服务函数表）传给模块；模块把 `BLOCKS_DRIVER`（驱动实现表）交回内核。
- **理由**：**模块不直接引用内核符号**（不需要符号解析表），只通过 `Api` 回调；
  内核升级时只要 `Api` 版本兼容，模块不用重编。
- **代价**：每个内核服务都要进 `Api` 表（要克制，只放必要的）。
- **推翻条件**：若驱动需要的内核服务太多（>40 个入口），说明边界没切好，回到符号解析方案。

### ADR-3：模块加载时机 = 启动期按设备自动加载；运行期手动作为调试口
- **选择**：`Discover` 阶段枚举完设备后，对"无驱动"的设备按名找 `BLOCKS:MODULES/<name>.MOD`。
- **补充**：`mod list / mod load / mod unload` 作为 shell 调试命令（不进启动关键路径）。
- **推翻条件**：若启动期加载导致启动时间明显变长（预计每个模块 <20 ms），改为延迟到首次使用。

### ADR-4：失败隔离的**第一版只做"契约 + 错误返回"**
- **背景**：你要的是"驱动挂不全盘挂"。
- **选择**：
  - 第一版：驱动必须通过 `Probe/Start/Stop` 返回错误码；内核把失败设备标记为"不可用"；
  - **不做**异常捕获与地址空间隔离（列在路线图，非本版）。
- **理由**：先让"积木能插拔"成立，再谈"积木崩了怎么办"；顺序反过来会卡死在异常处理上。
- **风险**：驱动里野指针仍会全挂 —— **这条要写进验收清单，不能含糊**。

### ADR-5：模块从 FAT 加载，信任模型 = 与内核同级信任
- **选择**：不签名、不验签；卡上的 `.mod` 视同内核代码。
- **理由**：当前阶段这是玩具系统；签名会引入密钥管理，成本远超收益。
- **但必须记录**：**任何人能写卡，就能执行任意代码** —— 这条写进文档，未来做安全启动时再处理。

### ADR-6：保留"单 ELF 也能跑"的构建模式
- **选择**：`MODULES=0` 时所有驱动按现在的方式静态链接（回归基线）；`MODULES=1` 时走 `.mod`。
- **理由**：**模块化不能牺牲可启动性**；任何一次改动都能回到单 ELF 对照，定位问题。
- **代价**：两套构建要同时维护（接受）。

---

## 2 · 接口草案（评审重点，先看形状不看细节）

### 2.1 内核 → 模块：`BLOCKS_API`（内核服务表）

```c
typedef struct BLOCKS_API {
    UINT32 Size;                     /* sizeof(BLOCKS_API)，模块用它判断兼容 */
    UINT32 Version;                  /* 主版本；不匹配拒绝加载 */

    /* 内存 */
    void *(*Alloc)(UINTN Size);
    void  (*Free)(void *Ptr);
    /* 日志 */
    void  (*Log)(const char *Text);
    /* 设备访问 */
    int   (*MapMmio)(BLOCKS_DEVICE *Dev, UINT64 *OutVa);
    int   (*ReadConfig)(BLOCKS_DEVICE *Dev, UINT32 Off, void *Buf, UINTN Len);
    /* 中断 */
    int   (*RegisterIrq)(BLOCKS_DEVICE *Dev, void (*Handler)(void *Ctx), void *Ctx);
    /* 时间/延迟 */
    UINT64 (*Now)(void);
    void   (*DelayUs)(UINT32 Us);
} BLOCKS_API;
```

**原则**：`Api` 表里只放"驱动确实需要、且三架构语义一致"的东西。**没想清楚的先不放。**

### 2.2 模块 → 内核：`BLOCKS_DRIVER`（插座）

```c
typedef struct BLOCKS_DRIVER {
    UINT32 ApiVersion;                  /* 模块要求的 Api 版本 */
    const char *Name;                   /* "hid" */
    UINT32 Class;                       /* INPUT / BLOCK / NET / DISPLAY */
    const BLOCKS_MATCH *Match;          /* PCI / DTB / FIXED，复用现有 DRIVER_MATCH */
    int  (*Probe)(BLOCKS_DEVICE *Dev);
    int  (*Start)(BLOCKS_DEVICE *Dev);
    int  (*Stop)(BLOCKS_DEVICE *Dev);
    int  (*Read)(BLOCKS_DEVICE *Dev, void *Buf, UINT32 Len);
    int  (*Write)(BLOCKS_DEVICE *Dev, const void *Buf, UINT32 Len);
    int  (*Ioctl)(BLOCKS_DEVICE *Dev, UINT32 Cmd, void *Arg);
} BLOCKS_DRIVER;

int BlocksModuleInit(const BLOCKS_API *Api, BLOCKS_DRIVER *Out);
```

**关键点**：`BLOCKS_DRIVER` 与现有的 `DRIVER`（`Include/Driver/Driver.h`）**字段一一对应**，
只是把 `Probe` 的签名从"自扫总线"改成"接受内核给的设备"。**这是本设计能否落地的关键**——
如果两者差异太大，就要重写全部现有驱动（不可接受）。

### 2.3 `.mod` 文件

```
ELF 文件
  .text/.rodata/.data   模块代码
  .blocks               元数据（只读）：
      magic  "BMOD"  u32 version
      char    name[32]
      u32     class
      u32     api_version_required
      u32     flags
      依赖列表（阶段 2 才用，先留空）
  BlocksModuleInit      导出符号（唯一入口）
```

---

## 3 · 落点（代码地图，评审时对照）

| 要做的事 | 落点 | 现状 |
|---|---|---|
| 设备发现编排 | **新增** `Core/Device/Discover.c` | 无 |
| 本架构总线枚举 | `HalDeviceEnumerate()` 原样保留 | x64 真扫 PCI；ARM/RISCV 硬编码 virtio |
| 设备树 | `Core/Device/Tree.c`（`DEVICE_NODE`） | 已存在 |
| 驱动匹配 | `Core/Driver/Match.c`（PCI/DTB/FIXED） | 已存在 |
| 驱动注册表 | `Core/Driver/Driver.c`（`DriverRegister`） | 已存在，静态调用 |
| 模块加载器 | **新增** `Core/Module/Loader.c` | 无 |
| ELF 加载/重定位 | `Core/Elf/**` | 已存在（用于用户态） |
| 模块文件读取 | `Services/FileSystem` | 已存在（FAT 在核内） |
| 模块表 | `BringUp/Modules.c` | 已存在（插入 Discover 项） |

---

## 4 · 分步计划（每步可独立回退）

| 步 | 内容 | 回退方式 | 判据 |
|---|---|---|---|
| **S1** | 抽出 `Discover` 阶段（不动驱动） | 把 `DeviceInitialize/DeviceEnumerateAll` 搬回 `DriverInitialize` | x64 启动日志多出设备清单，其余行为完全不变 |
| **S2** | 模块加载器（只加载、不绑定），加载一个 `hello.mod` 打印一行 | 关闭 `MODULES=1` | 串口出现模块打印 |
| **S3** | `hid.mod`：注册到现有 `DRIVER` 表并绑定 | `MODULES=0` 回到静态 HID | 删 `HID.MOD` 鼠标不动；放回鼠标动 |
| **S4** | 显示模块（x64 GOP） | 同上 | 删显示模块仍启动（串口）、装回出画面 |
| **S5** | ARM64/RISC-V 的 DTB→设备树 | 沿用硬编码枚举 | Pi/Duo 设备清单来自 DTB |
| **S6** | 隔离评估（异常捕获可行性） | 不实现，只出结论 | 一份评估报告 |

**每步都要能"单独回退"，不允许 S3 依赖 S2 的临时代码。**

---

## 5 · 风险清单（按严重度）

| # | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R-1 | **模块 ABI 一旦定型就难改** | 所有 `.mod` 要重编 | `Api.Version` + `Api.Size` 双重校验；第一版只暴露 8 个入口 |
| R-2 | **驱动野指针仍会全挂**（ADR-4 的代价） | 达不到"不重蹈 Linux 覆辙" | 写进验收清单；S6 出异常捕获评估 |
| R-3 | **重定位/代码模型**：模块要加载到任意地址 | x64 非 PIE、RISC-V 相对寻址差异 | 复用 `Core/Elf`（三架构都验过），先只支持小代码模型 |
| R-4 | **内存分配**：模块要内存，但可能在 VMM 之前加载 | 早期无堆 | 模块只在 VMM 之后加载（Discover 在 VMM 之后） |
| R-5 | **启动变慢** | 每个模块加载有开销 | 目标 <20 ms/模块；超了改延迟加载 |
| R-6 | **两套构建并存**（MODULES=0/1） | 维护成本 | 每次改动两个模式都编（CI 里加） |
| R-7 | **卡上 `.mod` 就是代码执行** | 无安全边界 | 明确记录（ADR-5）；未来做签名 |
| R-8 | **ARM64/RISCV 枚举仍是假的** | 只有 x64 真闭环 | S5 之前不宣称"三架构模块化完成" |
| R-9 | **驱动依赖内核服务过多** → `Api` 爆表 | 回到符号解析方案 | 每次新增入口要评审 |
| R-10 | **现有 `DRIVER_MAX_DRIVERS 20` 上限** | 模块多了不够 | 改成动态表（S2 时一起做） |

---

## 6 · 给 Cursor 的评审问题（请逐条回答，可以直接反驳）

### 关于契约
- **Q-1** `BLOCKS_DRIVER` 与现有 `DRIVER`（`Include/Driver/Driver.h`）只差 `Probe` 签名，
  这个改动是否真的能让现有 20 个驱动**只改注册方式、不改实现**？有没有反例？
- **Q-2** `BLOCKS_API` 只放 8 个入口够不够？**HID 驱动**照现有实现，实际需要哪些内核服务？
  （请直接看 `HAL/X64/Drivers/Input/UsbHid/` 现有代码回答）
- **Q-3** 模块入口 `BlocksModuleInit(Api, Out)` 与内核"符号解析"方案相比，
  哪个对**三架构**更稳？理由。

### 关于隔离
- **Q-4（已核实一半）** x64 的异常分发现成的：`HAL/X64/Architecture/Dispatch.c` 里有
  `ArchIrqNotifyRegister(Vector, Handler)`（:46）和异常帧解码（:208 专门判 `Vector==14` 缺页）。
  所以"模块内异常 → 记录并卸载"**技术上可行**。
  **要问的**：第一版就做，还是先只做"错误返回"？做的话，判据怎么定（模块内一条 `*(volatile int*)0=0` 能卸载且系统存活）？
- **Q-5** 模块卸载（`Stop` + 释放）在什么条件下才安全？现有 `DRIVER.Remove` 有没有类似语义可复用？

### 关于发现
- **Q-6** 现在 ARM64/RISCV 的 `HalDeviceEnumerate` 是**硬编码 virtio**（`HAL/Arm64/DeviceEnum.c`、
  `HAL/RiscV/DeviceEnum.c`）。改成 DTB 遍历后，QEMU virt 的路径怎么保持？要不要双路径？
- **Q-7（发现一个真缺口）** `Include/Hal/HalIrq.h` 只有 `HalIrqEnable/Disable/Save/Restore`，
  **没有"注册某个设备的中断处理"的跨架构 API**；x64 侧是 `ArchIrqNotifyRegister`（架构私有）。
  这意味着我草案里的 `BLOCKS_API.RegisterIrq` **现在没有底层支撑**。
  **要问的**：现有 AHCI/HID 是怎么拿到中断的？（直接调 `ArchIrqNotifyRegister`？轮询？）
  如果都是轮询，那第一版 `BLOCKS_API` **就不要放 RegisterIrq**，等驱动真的需要再加。

### 关于工程
- **Q-8** S1（抽出 Discover）会不会影响 `BringUp/Modules.c` 里三张表的顺序约束（PMM→VMM→DISPLAY→…）？
- **Q-9** `MODULES=0/1` 两套构建，CI 里怎么保证不漂移？
- **Q-10（已核实，不必回答）** FAT 支持 LFN：`Library/Fat/DirectoryName.c` 会生成 8.3 别名，
  `DirectorySlot.c:203` 注释写明"始终写 LFN"。所以 `BLOCKS:MODULES/HID.MOD` 可行。

### 关于范围
- **Q-11** 你认为这一版设计**最可能在哪个环节翻车**？请给出一条"如果只验证一件事，验证什么"。
- **Q-12** 有没有更简单的做法能达到同样目的（比如：**只把驱动做成编译期可裁剪的独立 .o + 稳定 ABI**，
  而不做运行期加载）？如果有，代价是什么？

---

## 7 · 验收矩阵（定稿后照着执行）

| 能力 | 验证方式 | 通过标准 |
|---|---|---|
| Discover 阶段 | x64 启动 | 设备清单打印，块设备/键鼠/GUI 全部照旧 |
| 模块加载 | `hello.mod` | 串口出现模块输出；删文件不影响启动 |
| HID 模块化 | 删/放 `HID.MOD` | 鼠标不动 / 恢复 |
| 显示模块化 | 删/放显示模块（x64） | 串口可用 / HDMI 出画面 |
| 无驱动降级 | 删掉网卡模块 | 系统启动正常，仅无网络 |
| 三架构一致 | Pi / Duo 设备清单 | 来自 DTB，不是硬编码 |
| 回退 | `MODULES=0` 构建 | 与改造前行为一致 |

---

## 8 · 本次核实暴露的缺口（设计必须处理，否则 S2/S3 会卡）

| # | 缺口 | 事实 | 建议 |
|---|---|---|---|
| G-1 | **没有跨架构的 IRQ 注册 API** | `HalIrq.h` 只有开关/保存现场；x64 用私有的 `ArchIrqNotifyRegister` | 第一版 `BLOCKS_API` **不放 RegisterIrq**；驱动先轮询，等真有需要再补 `HalIrqRegister` |
| G-2 | 驱动表上限 20（`DRIVER_MAX_DRIVERS`） | 静态数组 | S2 时改动态表 |
| G-3 | `HalDeviceEnumerate` 在 ARM64/RISCV 是硬编码 virtio | `HAL/Arm64/DeviceEnum.c`、`HAL/RiscV/DeviceEnum.c` | S5 用 DTB 遍历替换，QEMU virt 走双路径 |
| G-4 | 设备节点里没有 IRQ 字段 | `DEVICE_NODE` 只有 bus/vid/did/class/BAR | 若 G-1 要补，`DEVICE_NODE` 也要加 `Irq` 字段 |
