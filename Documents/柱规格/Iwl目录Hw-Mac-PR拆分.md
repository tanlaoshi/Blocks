# Iwl 目录拆 Hw / Mac — PR 拆分

> 状态：待看。还没改代码。
>
> 前置：`Iwl/Crypto/` 已在 [驱动边界清理](驱动边界清理-PR拆分.md) `PR-DRV-iwl-crypto` 落地。本柱只整理 **Intel 8265/8275（iwlwifi）驱动目录**，不抽出全核无线栈。
>
> 对标 Linux：芯片侧（`iwlwifi`/`iwlmvm`）与 MAC 逻辑（扫/关联/EAPOL）分界；Blocks 现阶段用**目录**表达，接口仍可留在 `IwlPrivate.h`，不必先造 `mac80211`。

## Iwl 是什么

板载 **Intel Wireless LAN**（PCIe `8086:24FD`），目录 `HAL/X64/Drivers/Iwl/`。灌 `FW/IWL8265.UCODE`，读 `FW/WIFI.CFG`，关联后经 `NetIwl` 上网。与 USB `Wifi/`（8188EU）无关。

## 读完应有的样子

```text
Iwl/
  Crypto/          # 已有：AES/SHA/TKIP/CCMP/PBKDF（仍姓 Iwl，非全核公用）
  Hw/              # PCI、MMIO、固件、FH/TX/RX 队列、Alive、Paging
  Mac/             # Scan / Assoc / Eapol（可由现有子目录迁入或改名为 Mac 下）
  IwlPhy/ IwlMvm/  # 仍可留：校准与固件宿主命令，偏 Hw；本柱刀表写死归属
  NetIwl.c         # Driver Net 类胶水，留在 Iwl 根或 Hw 旁
  Iwl.h            # 对外 API 不动
  README.md        # 三行说明：Hw / Mac / Crypto
```

读法：

- 只关心「卡怎么认、固件怎么灌」→ 只进 `Hw/`
- 只关心「扫 AP、四次握手」→ 只进 `Mac/`
- 密码学 → `Crypto/`（调用方仍是 Mac）

## 明确不做

- **不**把 Scan/Assoc/EAPOL 搬出 `Drivers/Iwl` 变成 `Services/` 或 `Library/`（那是全核 `WifiMac` 柱，另立）。
- **不**改符号名对外契约（`IwlSetup`、`IwlSendFrame`、`IwlPoll` 等保持）。
- **不**改固件路径、WIFI.CFG 键名、PCI DID。
- **不**把 Crypto 升格为全核公用库（中性命名另柱；现无第二调用方）。
- 不碰 USB 窄口柱。

## 刀

每刀 `build.sh`。有无线行为的刀在 NUC 上手测关联（无 NUC 则 QEMU 无卡软失败不挡启动）。

| 序 | PR | 改哪些 | 本刀不碰 | 验收 |
| --- | --- | --- | --- | --- |
| 1 | **PR-IWL-hw-dir** | 新建 `Iwl/Hw/`。迁入明显硬件文件：`IwlHw.c`、`IwlFh.c`、`IwlFhDiag.c`、`IwlFw.c`、`IwlProbe.c`、`IwlAlive.c`、`IwlPaging.c`、`IwlRegs.h`（若仅 Hw 用）、以及现有 `IwlTx/`、`IwlRx/` 整个子树。Makefile 已有 `Iwl/*/*.c`，确认 `Hw/` 与 `Hw/*/` 被编入 | 协议文件不搬 | `build.sh`。无卡软失败。符号名不变 |
| 2 | **PR-IWL-mac-dir** | 新建 `Iwl/Mac/`。迁入 `IwlScan/`、`IwlAssoc/`、`IwlEapol/`（可保持子目录名，或变成 `Mac/Scan` 等；刀内写死一种，推荐 **原样子目录挂到 Mac/ 下** 以减小 diff：`Mac/IwlScan/…`）。根上若有纯编排的 `IwlMac.c` 一并归 Mac | 固件/FH | `build.sh`。NUC 有条件：仍能 `wpa2=ok` 或与搬前同级日志 |
| 3 | **PR-IWL-phy-mvm** | `IwlPhy/`、`IwlMvm/` 明确归属：**默认算 Hw**（固件宿主与校准），迁入 `Hw/IwlPhy`、`Hw/IwlMvm`，或在 README 写「驻留根下但属 Hw」。推荐迁入 Hw 以免根上再堆 | 算法 | `build.sh` |
| 4 | **PR-IWL-private-split** | `IwlPrivate.h` 拆成 `Hw/IwlHwPrivate.h` + `Mac/IwlMacPrivate.h`（或薄转发：Private 只 include 两者）。Mac 文件不声明 MMIO 细节；Hw 不声明 EAPOL 状态机字段（共享全局可留在一个 `IwlState.h`） | 行为 | `build.sh`。Mac 下 `.c` 无直接 `IwlMmio*` 为佳（若仍有，列在债表下刀清） |
| 5 | **PR-IWL-readme** | `Iwl/README.md`：Hw / Mac / Crypto 分工、与 Linux iwlwifi/mac80211 的对照一句、开机 Claim 仍在 Worker。链到本柱规格 | 代码 | 新人按 README 能回答「握手在哪」 |

## 建议顺序

```text
PR-IWL-hw-dir
  → PR-IWL-mac-dir
  → PR-IWL-phy-mvm
  → PR-IWL-private-split
  → PR-IWL-readme
```

先搬家、后拆私头、最后写说明。若某刀 diff 过大，可按子目录再拆（例如 Tx/Rx 单独一刀），但不要插入别的柱。

## 做完仍然在的

- 行数几乎不减；只是打开目录时路径变短。
- Crypto 仍姓 `Iwl*`，不是公用库。
- 全核共用的 802.11 MAC（第二家芯片复用握手）仍未做 → 见下。

## 另立柱（将来）

| 事项 | 何时开 |
| ---- | ---- |
| 全核 `WifiMac` / `Ieee80211` | 第二块无线驱动要共用扫/关联/EAPOL，或开课专讲无线栈 |
| Crypto 升格 `Library/Crypto` | 出现第二调用方，且去掉 `IwlPrivate` 依赖 |
