# 16 · Kernel 模块化 —— 设计稿 v3（沟通中）

> v1 原话与现状；v2 判定规则 + 开机到鼠标链路；v3 硬件发现（插座已存在）；
> **v4 定案：自动按设备加载、新增 Discover 阶段、`HalDeviceEnumerate` 保留为"总线枚举钩子"、
> X64 NUC 为主线先打通。**

---

## 1 · 原话（逐句保留）

### 第一轮

> 系统既然叫 Blocks，想必你也知道这是积木的意思，你知道乐高的那种积木底盘吗？
> 一个大平板底座，上面全是标准的乐高插座，也就是一个一个的小圆柱体。
> 我想把 Blocks 打造成那个底座，底座之下可能有无数的硬件，但是底座之上都是统一标准的，三架构都是一样的。这是第一点。

> 第二点，现在所有的东西都编译到 Kernel，太重了，随便改一点都要重编内核，
> 我不想重蹈 linux 的覆辙，驱动挂全盘挂。
> 我想把非各架构共性的东西都拿出去，最重的就是驱动。
> 三架构比较统一的都需要 CPU，内存初始化，然后是框架类的，X64 需要 PCIE，USB 栈，
> 这一条路下去切到具体驱动，到键鼠驱动。键鼠驱动就能拿出去了。
> 通用的 PCIE 和 USB 可以留着。ARM 和 RISCV 跟 X64 不一样，但是也有 CPU 和内存。

### 第二轮

> 我想的是内核模块做成 `.mod` 文件，Kernel.elf 编制流程，核心的串口、CPU、内存、FAT 等留在 Kernel.elf，
> 具体留哪些我们从开机到鼠标指针出现一点点抽。
> 目标是把三架构各自核心的流程抽出来，也就是只要是本架构下的电脑都有的部分。

### 第三轮

> Video 这种我想的是能否在 Kernel 初期获得当前机器上到底有什么硬件？
> X64 的 UEFI 应该能获得，ARM 和 RISCV 的 DTB 也应该能获得，如果知晓当前硬件都有哪些，就能按照硬件加载驱动。
> X64 可以通过 Kernel 初期的 PCIE 扫描获得设备列表，然后加载对应驱动，也就是一个一个的 mod 文件。
> 当前设备管理器已有扫描硬件逻辑，DTB 我还不是很确定。

### 第四轮

> Q1 应该自动，看有什么设备就加载什么；Q2 可以新增，但是当前的 HalDeviceEnumerate 怎么处理呢？
> Q3。三架构的机器我都有，最适合的是 X64 的 NUC，它是主线。

---

## 2 · 判定规则（沿用 v2）

```
留 Kernel.elf ⟺ 本架构下任何一台机器都必然有
做成 .mod    ⟺ 只有某些机器/某些外设才有
三问：换主板要改代码吗 / 没它能否启动到串口 / 依赖的地址引脚是否同架构一致
```

---

## 3 · 【重要】插座已经存在了（v3 最大发现）

你的设想在代码里**已经有骨架**，不是从零开始：

| 你要的东西 | 代码里已经有的 | 位置 |
|---|---|---|
| 驱动契约（插座） | `DRIVER`：`Name / Class / Match / Probe / Bind / Remove / MaxInstances` | `Include/Driver/Driver.h` |
| 设备匹配（认领） | `DRIVER_MATCH`：**PCI（vendor/device/class） / DTB（compatible） / FIXED（固定地址）** | 同上 |
| 设备树（底座枚举结果） | `DEVICE_NODE` + `DeviceAdd()` | `Core/Device/Tree.c` |
| 匹配算法 | `DriverMatchesDevice()` | `Core/Driver/Match.c` |
| 注册表 | `DriverRegister(&drv)` + `DriverInitialize()` | `Core/Driver/Driver.c:192` |
| 枚举入口 | `HalDeviceEnumerate()`（x64：遍历 256 总线×32 设备×8 功能，读 VID/DID/Class） | `HAL/X64/DeviceEnum/DeviceEnum.c:131` |

**`DriverInitialize()` 现在做的事就是标准流程**：

```
DeviceInitialize()        ← 清表
DeviceEnumerateAll()      ← 枚举硬件，填 DEVICE_NODE 树
HalDriverRegister()       ← 各驱动把自己的 DRIVER 注册进来（现在是静态调用）
DriverProbeClass(BLOCK)   ← 按类匹配、Probe、Bind
```

**⇒ 模块化不是"发明插座"，而是"把 `HalDriverRegister` 里的静态注册搬到 `.mod` 里"。**
这句话把工作量降了一个数量级。

---

## 4 · 三架构的硬件发现能力（现状，逐条核实过）

| 架构 | 发现源 | 现在真正拿到的 | 缺口 |
|---|---|---|---|
| **X64** | UEFI 交接 + **PCIe 扫描** | 完整设备树（VID/DID/Class/BAR/IRQ）→ `DEVICE_NODE` | ACPI/RSDP 只在 handoff 里传（`HalPlatform*`），未用于驱动绑定 |
| **ARM64** | DTB（u-boot `x0` / FAT 上的 `pi.dtb` / QEMU loader 放置） | 只有**内存区**（`DtbMemoryRegion`）和**CPU 数**（`DtbCpuCount`） | 没有遍历成 `DEVICE_NODE`（compatible/reg/interrupts） |
| **RISC-V** | DTB（u-boot `a1`；**我们自己的 FIT 里已嵌 `fdt-1`**） | 同上（内存区 + CPU 数） | 同上 |

### 关于 DTB 的确定答案（你说"不是很确定"）

1. **拿得到**：
   - Duo：`mk-boot-sd.py` 生成的 FIT 里**已经有 `fdt-1` 节点**，u-boot 启动时通过 `a1` 把 DTB 物理地址传进来；
   - Pi：FAT 上有 `pi.dtb`，u-boot 通过 `x0` 传（`BringUp/Arm64/KernelHandoff.c` 已处理 QEMU/Pi 两种来源）。
2. **已经有解析器**：`HAL/Virtio/Dtb.c`（175 行，FDT 头/结构遍历）+ `HAL/Virtio/DtbProbe.c`（`DtbCpuCount`）。
3. **现在的用法只有两处**：内存区、CPU 数（SMP 拉核）。
4. **要补的**：把 `/soc/...` 下的节点（`compatible`、`reg`、`interrupts`）遍历成 `DEVICE_NODE`，
   这样才能和 `DRIVER_MATCH.Dtb.Compatible` 对上 —— **契约已经支持，缺的是"喂数据"这一步**。

---

## 5 · 于是模块化的落点变得很清楚

### 5.1 两件事，不是十件

```
① 统一"发现"：三架构都产出 DEVICE_NODE 树（x64 已有；ARM/RISCV 补 DTB→DEVICE_NODE）
② 统一"注册"：DriverRegister 从静态调用改成运行期由 .mod 调用
```

### 5.2 目标形态

```
开机：
  Serial → PMM → VMM → Video(有 FB 才启) → Cpu
    ↓
  Discover  ← 底座的核心新增物
    x64  : PCIe 扫描 (+ACPI)          → DEVICE_NODE 树
    ARM  : DTB /soc/* compatible/reg → DEVICE_NODE 树
    RISC-V: DTB /soc/*               → DEVICE_NODE 树
    ↓
  对每个设备节点：查已加载模块 → 有匹配就 Probe/Bind
    ↓
  没有驱动的节点：去 BLOCKS:MODULES/ 找同名 .mod → 加载 → 再 Probe
    （找不到就把设备标记"无驱动"，**不影响启动**）
    ↓
  FileSystem → Scheduler → Console → [Gui 若 Video 存在] → [Network 若网卡绑定成功]
```

**这正好实现你说的"按硬件加载驱动"**：设备树是底座给的，驱动是插上去的。

### 5.3 Video 的归属（回答你第三轮点名的例子）

- **Video 的"发现"部分留内核**：有没有帧缓冲、多大、什么格式 —— 这是 `BOOT_HANDOFF` 已有的（`FrameBufferBase/Size/分辨率/GOP`）；
- **Video 的"具体实现"做成 `.mod`**：
  - x64：GOP 线性帧缓冲 / 以后可以加显卡驱动；
  - Pi：mailbox framebuffer（`BoardFbSetup`）；
  - Duo：无 FB（串口 console）→ **不加载任何显示模块**，Gui 不启动。

⇒ **判据**：Duo 上删掉显示模块，系统照样启动到 `Blocks>`；Pi 上恢复模块，HDMI 出画面。
**这就验证了"按硬件加载"**。

---

## 6 · 建议的第一批模块（按风险从低到高）

| 批次 | 模块 | 为什么先做它 | 判据 |
|---|---|---|---|
| **1** | **HID 键鼠** | 坏只丢输入；三架构都有输入设备；契约现成 | 删 `HID.MOD` 鼠标不动、系统正常；放回鼠标动 |
| 2 | 显示（GOP / mailbox） | 直接验证"按硬件加载"（Duo 没有就不加载） | Duo 无显示模块仍启动；Pi 有则出画面 |
| 3 | 网卡（含 Duo WiFi） | 最复杂，放最后 | 加载后能 DHCP |

---

## 7 · 待你拍板（只剩 3 条）

### Q1 `.mod` 加载时机
- A. 启动时按需加载（枚举到没驱动的设备 → 找同名 `.mod`）  ← 我倾向
- B. 启动后手动（`mod load hid`）
- C. A 为主 + B 作为调试入口  ← **我实际建议这个**

### Q2 Discover 阶段放在哪
- A. 在 `DriverInitialize()` 之前新增一个 `HardwareDiscover` 模块（新表项） ← 我倾向
- B. 揉进现有的 `HalDeviceEnumerate`

### Q3 ARM/RISCV 的 DTB→DEVICE_NODE 谁来写
这是我们第一次真正"补底座"，涉及 `HAL/Virtio/Dtb.c` 扩展 + 三架构对齐。
**我建议先只做 RISC-V（Duo 手上有板子，能立刻验证）**，ARM 随后。

---

## 8 · 下一步（建议顺序）

1. **你确认 §7 三条**；
2. 我出一份"Discover 接口"的头文件草案（`DEVICE_NODE` 已有，补"资源/类/匹配键"三样）；
3. 先做 RISC-V 的 DTB→设备树（不动驱动、不改现有流程，只在新阶段填表并打印）；
4. 再抽 HID 成 `.mod`（这就是第一块积木）。

**做到第 4 步，"底座 + 插座 + 积木"三件套就都立起来了。**

---

## 9 · v4 定案（本轮结论）

### 9.1 Q1 = 自动加载（按设备）

```
启动 Discover → 拿到设备树
  对每个设备：
    ├─ 已加载的模块里有匹配     → 直接 Probe/Bind
    ├─ 没有匹配                → 去 BLOCKS:MODULES/ 找模块
    │                            · 找到且 Probe 返回 0 → 加载并绑定
    │                            · 没有模块           → 标记"无驱动"，继续
    └─ 模块加载失败            → 标记"加载失败"，继续（绝不阻断启动）
```

**硬规则：发现阶段任何一步失败，都只降级、不阻断。** 没有网卡就是没网，不能因此起不来。

### 9.2 Q2 = 新增 Discover，`HalDeviceEnumerate` **保留但要降级为"总线枚举钩子"**

现状（三条事实）：

| 架构 | `HalDeviceEnumerate()` 现在做什么 | 评价 |
|---|---|---|
| **X64** | 真扫 PCI：256 总线 × 32 设备 × 8 功能，读 VID/DID/Class → `DeviceAdd` | ✅ 真实、可用（主线就用它） |
| **ARM64** | **硬编码**三个 virtio 设备（`AddMmio("virtio-blk",2)` 等） | ⚠️ 只对 QEMU virt 成立，Pi 真机不对 |
| **RISC-V** | 同上，且仅 `BOARD_IS_VIRT` 时加 | ⚠️ Duo 真机什么都不加 |

调用链：`DriverInitialize()` → `DeviceEnumerateAll()` → `HalDeviceEnumerate()` → `DeviceAdd()`

**处理办法：不删、不重写，重新分层。**

```
Core（底座，三架构共用）
  HardwareDiscover()            ← 【新增阶段】编排 + 打印 + 标注无驱动
      │
      ├─ ① HalBusEnumerate()    ← 【改名/保留】本架构的总线枚举钩子
      │      x64 : PCIe 扫描（= 现在的 HalDeviceEnumerate 原样搬过来）
      │      arm64/riscv: 先沿用现在的实现，后续换 DTB 遍历
      │
      ├─ ② 打印设备清单（DEBUG 可见）
      ├─ ③ 标出"无驱动"的设备（给模块加载器用）
      └─ ④ 调模块加载器按设备找 .mod → Probe 成功的 Bind
```

**具体到改动量**：

- `HalDeviceEnumerate` **一行代码都不改**，只把它的调用点从 `DeviceEnumerateAll()` 移到新阶段；
- `DeviceEnumerateAll()` 保留（内部就是 `HalDeviceEnumerate + 自检`），作为"本架构枚举"的入口；
- `DriverInitialize()` 里的 `DeviceInitialize(); DeviceEnumerateAll();` 两行移到 `HardwareDiscover()`；
  它自己只留 `register → probe`，且 register 的来源从"静态调用"改成"模块加载器加载进来的"。

**这样 x64 一分钟都不受影响**（枚举逻辑原样），风险最低。

### 9.3 Q3 = X64 NUC 为主线

**主线顺序**（X64 上先把整条链跑通，再移植）：

```
第 1 步  HardwareDiscover 阶段落地（x64）
         · 把 DeviceInitialize/DeviceEnumerateAll 移到新阶段
         · 打印设备清单：bus/vid:did/class/有无驱动
         判据：NUC 启动日志里能看到同一份设备清单，且启动不变

第 2 步  HID 抽成 hid.mod（x64）
         · 内核侧：INPUT 类设备若无驱动 → 加载 BLOCKS:MODULES/HID.MOD
         判据：删掉 HID.MOD → 系统正常启动、鼠标不动；放回去 → 鼠标动

第 3 步  显示模块抽出（x64 GOP）
         判据：删掉显示模块 → 仍能启动（串口 console），装回 → 出画面

第 4 步  按同一套接口补 ARM64(Pi) / RISC-V(Duo) 的 DTB 枚举
         判据：Pi/Duo 的设备清单来自 DTB，而不是硬编码的 virtio 列表
```

### 9.4 第一块要动的代码（第 1 步的最小切片）

1. 新增 `Core/Device/Discover.c`（或 `BringUp/Initialize/Discover.c`）：
   `HardwareDiscover()` = `DeviceInitialize → DeviceEnumerateAll → 打印清单 → 标注无驱动`；
2. 模块表：在 `Cpu` 之后、`Driver` 之前插入 `{ "Discover", HardwareDiscover }`；
3. `DriverInitialize()` 删掉那两行（枚举已在新阶段做过）；
4. 判据：**x64 同一份构建，启动日志多出设备清单一行，其余行为不变**（回归：块设备/键鼠/GUI 都正常）。

**这一步不引入模块加载、不改任何驱动** —— 只是把"发现"从 Driver 里抽出来独立成一相。
**它是后面所有工作的地基，且几乎零风险。**
