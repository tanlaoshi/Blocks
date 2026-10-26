# Blocks 大事记

> **用途**：从 **ToyBoot / ToyKernel / ToyImage** 早码，到「Blocks 单仓 + Pi/Duo/网/字」的**重大跃迁**年表。只记质变，不记每刀。  
> **详对话**：[`Blocks诞生史.md`](Blocks诞生史.md)（本机抽问 + 第三台编年全文）。  
> **习惯复盘**：[`协作历程日志.md`](协作历程日志.md)（Toy 名时期主会话 A～I）。  
> **证据**：`tanlaoshi/ToyBoot` · `ToyKernel` · `ToyImage` git log/tag；现仓 `Blocks` `origin/main`。  
> **整理日**：2026-10-27。三机状态：Box 已采；第三台编年已并入诞生史；家里早期机仍待 `_inbox`。  
> **口径**：项目**不是** 9 月才开。git 上 **2026-08-26 ToyBoot v0.1** 已是种子；诞生史里「### 1. 发问」（09-01 fork.s）只是 Box 上最早留下的短会话编号，**不是创世**。

---

## 怎么读

| 标记 | 含义 |
| --- | --- |
| ★ | 能力或产品形态跃迁（用户能感到「不是同一个系统了」） |
| ◆ | 协作/仓体/命名跃迁（怎么做 OS 的方式变了） |
| ● | 版本或封版节点（可打 tag / 可对另一台说「拉这个」） |

日期以**提交日**为准（创世看 ToyBoot）；对话日仅作辅证。同一周内多刀合并叙述。

---

## 2026-08 · ★ 创世（权威：`/home/tank/tanlaoshi/edk2` + `edk2backup`；GitHub 浅史截断）

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| **08-26** | ●★ | 同日 **ToyBoot v0.1**（15:44，单文件 GOP+ELF）+ **ToyKernel v0.1**（16:03，Kernel+Video+UI） | Boot `7fb59eb`；本机 Kernel `e624fe0` |
| 08-28 | ★ | Boot+Kernel：**PCIe / xHCI / 键盘检测** | Boot `e79dac5`…；Kernel `1143a13` `32f7991` `3bd04f9` |
| **08-31** | ★ | **「添加了很多功能」**：common 模块爆炸（FAT/GPT/Gui/Net/Sched/Syscall/XHCI…）+ 三架构 hal 胚；同日 **窗口拖动** | 本机 `1e7099e` `de755ab` |
| **09-01** | ●★ | **ToyImage** 首提「For testing ToyKernel」（EFI+ELF+HELLO…）；Kernel「Add a lot」+ **PR-1～4** 分层门面；G1/G2 关窗拖窗 | Image `11c2957`；Kernel `653e08d`… |
| 09-02～04 | ★ | lwIP 柱、COW fork、SMP、D1～D7 桌面主题、G6～G10 合成……（本机连续） | `tanlaoshi/edk2/ToyKernel` log |
| 10-02 | ◆ | **钉死 edk2 备份 → 迁 `~/ToyOS`**（BOX-0…7）；`tanlaoshi/edk2` 比 `edk2backup` 多 BOX/nic 段 | `88fbb08`…`fbb136f`；工作树 `/home/tank/tanlaoshi/edk2` |
| （后） | ◆ | GitHub 浅史常从 09-24 `0a66698` 起看；**本机仍有自 `e624fe0` 全链** | `tanlaoshi/edk2/ToyKernel` |

**一句话**：**早码完整史在本机两份 edk2 树**（优先 `tanlaoshi/edk2`）；只看 GitHub 浅克隆会误判「Kernel 9-24 才出生」。

**易混**：诞生史 ### 1 ≪ 08-26；网上浅根 09-24 ≪ 本机 v0.1。

---

## 2026-09 上旬 · 积木、FAT、分层意识

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| 09 初 | ★ | **Console / Shell / ModuleInit[]**：模块像积木 | 诞生史阶段 B |
| 09 初 | ◆ | **EDK2 式全词命名**立项（先方案后动刀） | 协作历程阶段 B |
| 09 初～中 | ★ | 安装愿景收束：**FAT 系统盘 + 程序加载器**（放弃先做双系统/NTFS） | 协作历程阶段 D；`TOYOS.ID` 双盘 |
| 09-03～04 | ★ | Boot：**THEME.CFG 驱动 GOP**；优先 TOYOS 卷读 `Kernel.elf` | ToyBoot `e9ae4d8` `8cf8720` |
| 09-05 | ◆ | **PR-R1**：共享 handoff、EBS retry、ELF 边界 | ToyBoot `5a406aa` |
| 09 中 | ★ | **窗口拖动 / 合成 / 主题**长迭代开打（最大痛点簇） | 协作历程阶段 C/F |
| 09-06～08 | ◆ | HAL/X64 路径对齐；真机 GOP/COM1/ABI 跳转硬化 | ToyBoot `8c47498` `eaf877c` `bf4e2c1` |

**一句话**：产品路径钉成「课堂可演示的 FAT 桌面」，不是完整安装器。

---

## 2026-09-10 · ★ NUC 真机可用桌面

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| 09-10 | ★ | **Intel NUC 有线键鼠分口可用**（poll xHCI；辨假复合 HID） | 协作历程 §7；里程碑日 |
| 09-10 | ★ | Present / 壳打字 Present 合并（G-present 族） | 协作历程 §7 |
| 同期 | ◆ | 协作暗号清晰化：**JX / TG**（其后拆 **TS**） | 协作历程阶段 I |

**一句话**：从「家里能写、办公室能编」到「真机可坐下来用」闭环。

---

## 2026-09 下旬 · 设备树、GUI 控件、教学测试柱

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| 09-20 | ◆ | Boot.c **拆 Video / 读盘 / xHCI PCI**（S-boot-1…3） | ToyBoot `c63c9df`…`6d5c7dc` |
| 09-24 | ★ | **设备管理器 / DriverMatch / DeviceTree** 柱连收 | ToyKernel 09-24 系列 |
| 09-24 | ★ | **GUI 按钮控件**与多窗迁移；PMM/调度器教学 OPS 柱 | 同上 |
| 09-24 | ◆ | **test-shell / test-user / test-fs** 脚本化验收 | `0e0e0af` `bb291ca` `8011d6c` |

**一句话**：课堂不只「能跑」，开始「能讲、能量、能回归」。

---

## 2026-10-01～04 · 商店、无线、树重构、封版 0.1.0

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| 10-01 | ★ | **CHAT 柱**：LAN 半双工聊天；NUC 闭环手测 | ToyKernel CHAT-0…4 |
| 10-02 | ◆ | **OpenBox / BOX-0…7**：钉迁出 edk2 → `~/ToyOS`；Scripts/env；Kernel 单仓可编 | `d1fc735`…`fbb136f` |
| 10-02 | ★ | Shell ↑↓ 历史；启动 Logo→清屏；网态三图标 | BOX-1…3 |
| 10-02～03 | ◆ | **坏味道柱 SMELL** + iwl/xhci 文件拆分波 | SMELL / F-iwl / F-xhci |
| 10-03 | ★ | **CJK 灰度点阵 → i18n → 色板 → UiLayout**；TREE CodeA–E | `b2a6f3f`…`133753f` |
| 10-03 | ★ | **uitxt 设计器 + Guest MYAPP** | `b0f2380` 等 |
| 10-04 | ★ | **igpu CopyOk / Scanout**；**TTF 柱 ttf-0…subset** | `3e1eac9`…`33d5950` |
| 10-04 | ● | **ToyOS 0.1.0 封版**（TTF CJK 子集与 UI 收口） | tag **`v0.1.0`** @ `ca0447e` / `20c4baf` |

**一句话**：0.1.0 =「教学桌面 + 中文矢量起步 + 可发版」的第一道线。

---

## 2026-10-05～07 · ★ 第三台：人民可读 → 0.1.1 → ToyOSNew

> 详录全文见诞生史「会话 · 第三台·ToyOS编年」。

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| 10-05 | ◆ | 接棒宣言：**人民的操作系统**；人话文件头规格 | 第三台 19:17 |
| 10-05 | ◆ | **KernelMain 顺序表四刀** seq-1…4（三路分发→三表 Run→Spawn→Init 拆文件） | `af0964c`…`0860932` |
| 10-05 | ★ | TaskMgr 人话头；**Apps 认盘上预装包**；ktdemo→**KernelTaskDemo** | `a0faaf7`；Image `1118317` |
| 10-05 | ● | **ToyOS 0.1.1** + tag **`v0.1.1`**（配 SSH 后首次推上 GitHub） | `8307707` |
| 10-05 | ◆ | READ-2/3：学生调度器 / Desktop 人话头；人民可读刀表收官 | `a214fa8` `efe3ce8` |
| 10-05 夜 | ◆ | 目录字母序=故事；**Class→Protocol→Family→Chip** 四层驱动钉名 | 方案文档 |
| 10-06 | ◆ | **换思路**：根只要 **Boot / Kernel / Runtime**；Build 分家；**ToyOSNew** 先文档不搬码 | 第三台 08:24 |
| 10-07 | ◆ | 方案拍板倾向 **OpenBox 单仓**；文档镜像推 Kernel 便于另一台 `pull` | `add7ea3` `46ed2b6` `d2119f6` |

**一句话**：从「Agent 密电刀号」转向「人能读的顺序表 + 将来单仓大树」。

---

## 2026-10 中旬 · OpenBox → Blocks 单仓

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| 10-08 | ◆ | OpenBox Boot 备忘改指真树；Build 按下沉 | ToyKernel `668e635` |
| 10 中 | ◆ | 工作目录迁到 **Blocks**；品牌 Toy→Blocks；去 Toy 命名波 | Blocks 诞生史阶段 L；结构刀 |
| 10 中 | ◆ | Documents→Archive；Manual→Start→**Bingo**；User/Package 归一 | Blocks `8242e24b` `27f1e736` 等 |
| 同期 | ★ | HAL 顶栏收成 **X64 / Arm64 / RiscV / Virtio + Common** | `8c57a528` |

**一句话**：仓名与目录故事终于和「积木 / Blocks」一致。

---

## 2026-10 下旬 · ★ 板级真机：Pi 与 Duo

| 日 | 级 | 事 | 证据 |
| --- | --- | --- | --- |
| ～10-25 | ★ | **Milk-V Duo S**：串口 `blocks>`；Sv39；µSD；FIT/`boot.sd`；HOST USB 键 | Blocks duo-* 提交链 |
| ～10-25 | ★ | **Duo 有线网** net-1…6（MAC→PHY→DMA→HalNet→ping→LwIP/DHCP）收官 | `08708e91`…`bb077609` |
| ～10-26 | ★ | **RPi 3B+**：mailbox FB 进桌面；DWC2 HID；SDHOST+TTF；EL1+MMU；FB Device 收花屏 | rpi-4…9 |
| 10-26 | ◆ | 日档 **1026** 收官（user / duo-net / rpi-harden）；开 **1027** | 活档 |
| 10-27 | ★ | **Pi 有线网** rnet-1…4（LwIP→L2→静态 ping→DHCP）收官 | `ce5ff815`…`ac455e9f` |
| 10-27 | ★ | **gui-cjk-ttf** cjk-0…4：UMing/Droid 挂设置；中文只 TTF；Logo 回方块加深 | `caa9e0de`…`fbe7d97a` |
| 10-27 | ◆ | 诞生史：并入**第三台**编年；抽本大事记；家里机仍待采 | 本文 / 诞生史文首 |

**一句话**：Blocks 不再只是 x86 QEMU/NUC 故事，三架构真机有线网与中文矢量进课堂口径。

---

## 主线一张图

```text
★ 2026-08-26 ToyBoot v0.1 + ToyKernel v0.1（创世；史在 tanlaoshi/edk2 ∥ edk2backup）
    → Cursor 接棒审三仓（代码已在）→ 能稳定进核
    → Console/积木 + FAT 桌面
    → GUI 体感长战
    → ★ NUC 键鼠（09-10）
    → 设备树 / 测试柱 / 商店·无线
    → ● 0.1.0（TTF+UI）
    → ★ 人民可读 + ● 0.1.1 + ToyOSNew/OpenBox 思路
    → ◆ Blocks 单仓 / 去 Toy
    → ★ Duo + Pi 真机与有线网
    → ★ GUI 中文全 TTF（进行中 cjk-5）
```

---

## 版本与仓对照

| 产品名时期 | 主要仓 | 关键 tag / HEAD 线索 |
| --- | --- | --- |
| ToyOS 三仓 | ToyBoot / ToyKernel / ToyImage | Boot `7fb59eb`…；Kernel **`v0.1.0`** **`v0.1.1`** |
| OpenBox / ToyOS 迁出 | 同左 → `~/ToyOS` | BOX-0…7；TREE；OpenBox 文档 |
| Blocks 单仓 | `tanlaoshi/Blocks` | `main`；日档 1025…1027 |

---

## 维护约定

1. **大事**才进本文：用户能感到的能力跃迁、封版、仓体/协作方式质变。单刀修 bug 默认只进诞生史或日档。  
2. 新机对话并入诞生史后，若出现新跃迁，**追加一行到对应月表**，勿改写已钉「一句话」。  
3. 提交 hash / tag 以 GitHub 为准；对话日与提交日差一天时，表内取提交日并在备注写对话日。  
4. 不占路线图 ★；TG 本文件时与诞生史同批即可。
'''
