# Duo WiFi · MM_START 调研（只读，对照厂商 D80 代码）

> 目的：回答"让 WiFi 真正能用"的下一道门 —— `MM_START` 到底要什么。
> 依据文件（全部为 SDK 内 D80 路径，行号以本仓 `Source/DUO/sdk/duo-buildroot-sdk-v2-2.0.1/linux_5.10/drivers/net/wireless/aicsemi/aic8800/` 为准）：
> `aic8800_fdrv/lmac_msg.h`、`aic8800_fdrv/rwnx_msg_tx.c`、`aic8800_fdrv/rwnx_main.c`、
> `aic8800_fdrv/rwnx_mod_params.c`、`aic8800_fdrv/aicwf_compat_8800d80.c`、`aic8800_bsp/aic_bsp_driver.c`。

---

## 0 · 结论速览

| 问题 | 答案 |
|---|---|
| `mm_start_req` 多大 | **70 B**：`phy_cfg`(16×u32=64) + `uapsd_timeout`(u32) + `lp_clk_accuracy`(u16) |
| `phy_cfg` 从哪来 | **全零**。`rwnx_hw->phy.cfg` 在整个 D80 代码路径里**没有任何赋值**（唯一的赋值在 `#if 0` 里），`rwnx_hw` 是 kzalloc |
| 要不要 efuse | 不用。MAC 地址走 `MM_GET_MAC_ADDR_REQ(0x73)` 由固件从 efuse 读回，主机不直接读 efuse |
| MM_START 前必须发什么 | `MM_RESET` → `MM_VERSION` → `ME_CONFIG` → `ME_CHAN_CONFIG`；`MM_START` 在 netdev open 时才发 |
| MM_START 之后 | `MM_ADD_IF`，然后才能 `SCANU_START`（扫描） |
| 失败会怎样 | 厂商只 `return error`，不做恢复；只要 DBG 读还通，固件就还在跑，可重试。只有 DBG 读也不通才需要重下固件 |

---

## 1 · `struct mm_start_req` 逐字段（`lmac_msg.h:540-547`）

```c
struct phy_cfg_tag {                    // lmac_msg.h:509-514
    u32_l parameters[PHY_CFG_BUF_SIZE]; // PHY_CFG_BUF_SIZE = 16
};
struct mm_start_req {
    struct phy_cfg_tag phy_cfg;   // 64 B
    u32_l uapsd_timeout;          //  4 B
    u16_l lp_clk_accuracy;        //  2 B
};                                // 合计 70 B（param_len=70）
```

| 偏移 | 大小 | 字段 | 厂商取值 | 我们该填 |
|---|---|---|---|---|
| 0 | 64 | `phy_cfg.parameters[16]` | 全 0（见 §2） | 全 0 |
| 64 | 4 | `uapsd_timeout` | `rwnx_mod_params.uapsd_timeout`，默认 **300**（`rwnx_mod_params.c:42`） | 300 |
| 68 | 2 | `lp_clk_accuracy` | `rwnx_mod_params.lp_clk_ppm`，默认 **20**（`rwnx_mod_params.c:61`） | 20 |

注意：`rwnx_send_msg(..., reqcfm=1, MM_START_CFM, NULL)`（`rwnx_msg_tx.c:490`）——厂商**不校验** CFM 内容，只看有没有 CFM。

---

## 2 · `phy_cfg` 的来源：全零（关键结论）

`rwnx_send_start()`（`rwnx_msg_tx.c:472-491`）只做：

```c
memcpy(&start_req_param->phy_cfg, &rwnx_hw->phy.cfg, sizeof(rwnx_hw->phy.cfg));
start_req_param->uapsd_timeout   = rwnx_hw->mod_params->uapsd_timeout;
start_req_param->lp_clk_accuracy = rwnx_hw->mod_params->lp_clk_ppm;
```

而全仓 `phy.cfg` 的写入点只有两处，**都在 `#if 0` 里**（`rwnx_mod_params.c:1605-1633`，
`rwnx_set_rf_params()` 整段被 `#if 0` 包住）：

* `rwnx_mod_params.c:1620` `memcpy(&rwnx_hw->phy.cfg, &phy_conf.trd, ...)`
* `rwnx_mod_params.c:1629` `memcpy(&rwnx_hw->phy.cfg, &phy_conf.karst, ...)`

`rwnx_hw` 由 `kzalloc` 分配 ⇒ `phy.cfg` **恒为 64 个 0**。
结论：**我们可以直接发全零 phy_cfg**，与厂商逐字节相同，不需要 efuse、不需要 phy 配置文件。

---

## 3 · 调用顺序（D80、testmode=0）

### 3.1 probe 阶段（下载完 fmacfw 之后，`rwnx_main.c:7050-7140`）

| 序 | 动作 | 消息 id / 说明 | 判据 |
|---|---|---|---|
| 1 | `rwnx_ic_rf_init` | D80 走 `aicwf_set_rf_config_8800d80()`（`aicwf_compat_8800d80.c:15-37`）：<br>`MM_SET_TXPWR_IDX_LVL_REQ(0x77)` → `MM_SET_TXPWR_OFST2X_REQ(0x7b)` → `MM_SET_RF_CALIB_REQ(0x69)` | 各自有 CFM；失败即 `-1` |
| 2 | MAC 地址 | `MM_GET_MAC_ADDR_REQ(0x73)` / CFM `0x74`（`rwnx_main.c:7072`） | CFM 里 6 B MAC |
| 3 | **`MM_RESET_REQ`** | `0x0000` / CFM `0x0001`（`rwnx_main.c:7089`） | 有 CFM |
| 4 | (可选) `MM_SET_TEMP_COMP_REQ` | 仅 `CONFIG_TEMP_COMP` | — |
| 5 | **`MM_VERSION_REQ`** | `0x0004` / CFM `0x0005`（`rwnx_main.c:7097`） | 有 CFM，`version_lmac` = 固件版本（真机已验：`0x06090101`） |
| 6 | `rwnx_handle_dynparams` | 纯主机侧 | — |
| 7 | **`ME_CONFIG_REQ`** | `0x1400` / CFM `0x1401`（`rwnx_main.c:7122`） | 有 CFM |
| 8 | `wiphy_register` + `rwnx_custregd` | 主机侧 | — |
| 9 | **`ME_CHAN_CONFIG_REQ`** | `0x1402` / CFM `0x1403`（`rwnx_main.c:7138`） | 有 CFM |

### 3.2 open 阶段（`rwnx_open`，`rwnx_main.c:938-1000`）

| 序 | 动作 | 消息 | 判据 |
|---|---|---|---|
| 10 | **`MM_START_REQ`** | `0x0002` / CFM `0x0003`，param = 70 B | 有 CFM（`error` 直接冒泡） |
| 11 | (可选) `MM_SET_TXPWR...` | D80 无 | — |
| 12 | (可选) coex | `CONFIG_COEX` 下 `rwnx_send_coex_req` | — |
| 13 | **`MM_ADD_IF_REQ`** | `0x0006` / CFM `0x0007`，`struct mm_add_if_req{mac[6], type, p2p}` | CFM 里 `status`+`inst_nbr` |
| 14 | 之后才是扫描 | `SCANU_START_REQ` = `0x1000` / CFM `0x1001` | CFM |

> 也就是说：**`MM_START` 不是跟着 version 走的，它是"网卡 up"时才发**；
> 但它要求 `ME_CONFIG`、`ME_CHAN_CONFIG` 已经发过（否则固件没有信道表，扫描会空转）。

---

## 4 · 失败时芯片会怎样

* 厂商对所有 MM 消息的处理就是 `if (ret) goto err_lmac_reqs;`（`rwnx_main.c:7089-7140`）——
  **没有复位、没有重下固件**，直接返回错误；
* `MM_START` 失败后固件仍在运行（DBG 消息照常应答），可以改参数重试；
* 唯一必须重下固件的场景：**DBG 消息也不再应答**（消息通道死），此时只能 `SdhciWifiPatchStateReset`
  + 重新 `write confirm`；
* 一个已验证的判据：**`MM_VERSION_REQ` 有 CFM ⇒ 固件活着**（DBG 任务在 ROM 里就有，MM 任务只有固件有）。

---

## 5 · 最小可用路径（目标：扫到 AP）

按依赖顺序，最少要发：

```
（前置：write confirm 5/5 已通过 —— 固件已在 RAM 并 start_app）
1. MM_RESET_REQ      (0x0000) → CFM 0x0001     ← 已有 wififw mmreset
2. MM_VERSION_REQ    (0x0004) → CFM 0x0005     ← 已有 wififw ver
3. ME_CONFIG_REQ     (0x1400) → CFM 0x1401     ← 最大的一块参数（HT/VHT/HE caps）
4. ME_CHAN_CONFIG_REQ(0x1402) → CFM 0x1403     ← 2.4G 13 信道表
5. MM_START_REQ      (0x0002) → CFM 0x0003     ← 本刀的主角，70 B
6. MM_ADD_IF_REQ     (0x0006) → CFM 0x0007     ← type=STA 或 MONITOR
7. SCANU_START_REQ   (0x1000) → CFM 0x1001     ← 扫描
```

**可以跳过的**（不影响"扫到 AP"）：

* `MM_SET_TXPWR_IDX_LVL_REQ` / `OFST2X` / `RF_CALIB`：校准，影响发射功率与精度，不影响收包；
  真机发射前建议补，但扫描阶段可以先不做（风险：发射功率默认值可能偏低）；
* `MM_SET_TEMP_COMP`（需要 `CONFIG_TEMP_COMP`）；
* coex（`CONFIG_COEX`）；
* 用户配置 `aic_userconfig_8800d80.txt`。

**风险点**：`ME_CONFIG_REQ` / `ME_CHAN_CONFIG_REQ` 是两块大结构（HT/VHT/HE 能力 + 信道表），
字段多但都是"能力声明"，可以照 `rwnx_msg_tx.c` 里的 `rwnx_send_me_config_req()` /
`rwnx_send_me_chan_config_req()` 抄默认值。**建议下一步就做这两条 + MM_START 三件套**，
先只发不解析，用 CFM 的有无确认固件接受。

---

## 6 · 消息 id 速查（按 `lmac_msg.h` enum 顺序数出；`LMAC_FIRST_MSG(task)=task<<10`）

| id | 名称 | task |
|---|---|---|
| 0x0000 / 0x0001 | `MM_RESET_REQ` / `_CFM` | TASK_MM=0 |
| 0x0002 / 0x0003 | `MM_START_REQ` / `_CFM` | TASK_MM |
| 0x0004 / 0x0005 | `MM_VERSION_REQ` / `_CFM` | TASK_MM |
| 0x0006 / 0x0007 | `MM_ADD_IF_REQ` / `_CFM` | TASK_MM |
| 0x0069 / 0x006a | `MM_SET_RF_CALIB_REQ` / `_CFM` | TASK_MM |
| 0x0073 / 0x0074 | `MM_GET_MAC_ADDR_REQ` / `_CFM` | TASK_MM |
| 0x0077 / 0x0078 | `MM_SET_TXPWR_IDX_LVL_REQ` / `_CFM` | TASK_MM |
| 0x1000 / 0x1001 | `SCANU_START_REQ` / `_CFM` | TASK_SCANU=4 |
| 0x1400 / 0x1401 | `ME_CONFIG_REQ` / `_CFM` | TASK_ME=5 |
| 0x1402 / 0x1403 | `ME_CHAN_CONFIG_REQ` / `_CFM` | TASK_ME |

（0x0000-0x0007 已由真机 `mmreset`/`ver` 验证；其余按 enum 顺序数出，落地时一律用宏名，不硬写数字。）
