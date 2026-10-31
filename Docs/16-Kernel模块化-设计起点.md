# 16 · Kernel 模块化 —— 设计稿 v3（沟通中）

> v1 记原话与现状；v2 定判定规则（本架构通用性）并抽出开机到鼠标的链路；
> **v3 回答你的第三条：硬件发现（UEFI/PCIe/DTB）—— 并带来一个好消息：插座其实已经存在了。**

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
