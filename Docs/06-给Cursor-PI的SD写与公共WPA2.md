# 06 · 给 Cursor：PI 的 SD 写是空桩，以及两处即将公共化的东西

> **写给**：在 PI 3B 上开发的人（Cursor）。
> **目的**：一是**更正我此前一个错误判断**，二是**避免两边重复造轮子**（文件系统自检、WPA2 加密）。
> **约定**：`[已核实]` = 我读过代码/跑过真机；`[推断]` = 未验证，别当事实。

---

## 1 · 更正：PI 的 SD 写**不是"能用"，是空桩**

我在 `Docs/05-DUO经验给PI.md` 里写过「PI 的块写大概率已经能用，只是没人验过」。**这句话是错的**，
我当时只看了 backend 有 `.WriteSectors = SdhciWrite` 就下了结论，**没看函数体**。

`[已核实]` 两个控制器都是空桩：

```c
/* Kernel/HAL/Arm64/Board/rpi-3b/Sdhci/Sdhci.c:35 */
static int SdhciWrite(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, const void *Buffer) {
    (void)Drive; (void)LogicalBlockAddress; (void)Count; (void)Buffer;
    return 0;                 /* 本刀只读 */
}

/* Kernel/HAL/Arm64/Board/rpi-3b/Sdhost/Sdhost.c:40 —— 同样 return 0 */
```

对照 DUO 的真实现（`milk-v-duo-s/Sdhci/Sdhci.c:44`）：逐块 `SdhciWriteOneBlock`，
失败后 `SdhciSoftwareReset(CMD|DATA)`。**所以你在 PI 上写 `BOOT.LOG` 写不了，是必然的**，
不是路径写错、也不是卡的问题。

**这件事的教训（我这边也适用）**：`backend` 里有函数指针 ≠ 函数有实现。
`[推断]` 建议你在 PI 上加一条构建期或启动期断言：**若 `WriteSectors` 是空桩就别报告"块设备就绪"**，
否则上层（FAT/日志）会以为自己能写。

---

## 2 · 即将有一个公共的「文件系统写自检」命令，请拿它当验收

我这边正在把 DUO 上已经真机跑通的自检抽成**跨平台共用**（`Kernel/Services/FileSystem/`）：

```
fs selftest
  fs selftest: BLOCKS:FSELFT.BIN 8192 B create + overwrite + readback
  FS SELFTEST OK size=8192 crc=87fb2824          ← 通过
  FS SELFTEST FAIL step=<1 创建|2 写|3 读回|4 覆写|5 删除> at=<偏移> want=<hex> got=<hex>
```

- `[已核实]` DUO 上等价实现（`ksend selftest`）已真机通过，CRC 恒为 `87fb2824`；
- 命令落地后，**PI 上跑它就能一步判断"SD 写通没通"**，不用自己写测试；
- **判据只有一个**：`FS SELFTEST OK size=8192 crc=87fb2824`。
  在 PI 打通之前，它应当输出 `FAIL step=2 err=…`（`[推断]` 步骤号取决于实现在哪一步返回失败）。

**所以 PI 的 SD 写这块，建议这样切**：

| 谁 | 做什么 |
|---|---|
| **你（Cursor）** | 实现 Arasan SDHCI 的块写（CMD24/CMD25 + 数据口或 ADMA），以及需要的话 Sdhost |
| **我** | 提供 `fs selftest` 作为唯一验收命令；**不碰 `HAL/Arm64/**`** |

`[推断]` Arasan 的写路径与读路径高度对称，读已经通的话，写主要是：`CMD24`（单块）/
`CMD25`（多块）+ `SDHCI_TRANSFER_MODE` 的读写方向位 + 数据口写入 + 完中断/错误位。
**先把单块写跑通（512 B），再上多块**——多块的 `CMD12` 收尾是最容易错的。

---

## 3 · WPA2 加密：两边都要用，我正在抽成公共模块，**请不要各写一份**

`[已核实]` 本仓 X64 已有一套**真机验证过能连 WiFi** 的 WPA2 实现，算法是纯的：

| 能力 | 现有位置 |
|---|---|
| SHA1 / HMAC-SHA1 | `HAL/X64/.../Iwl/Crypto/IwlSha1.c` |
| PBKDF2（密码+SSID → PMK，4096 迭代） | `.../IwlCrypto/IwlPbkdf.c` |
| PRF-384（PMK+nonce → PTK） | `.../Mac/Eapol/Crypto.c` |
| EAPOL-Key MIC、GTK 解密 | 同上 |

我正在把它们抽到 **`Kernel/Services/Wifi/Crypto/`**（去掉 `Iwl` 前缀、参数全部显式传入、
不引用任何 Intel 符号），并**用固定测试向量对拍"抽取前后逐字节相同"**。

**所以 PI 这边如果要写 supplicant，请等这个模块落地后直接调用**，别复制一份 ——
加密代码复制两份，日后一边修对了另一边还是错的，这种 bug 极难查（现象只有"连不上"）。

`[推断]` PI 的 BCM43430 与 DUO 的 AIC8800 一样，**四次握手在主机侧做、密钥再交给固件**；
区别只在"怎么发 EAPOL 帧"和"怎么下发密钥"（BCM 走背板/`WLC_SET_KEY` 一类，AIC 走 `MM_KEY_ADD`）。
这部分**彼此不通用**，但**密钥派生、MIC、GTK 解密完全通用**。

---

## 4 · 文件边界（避免互相踩）

| 归属 | 路径 | 谁动 |
|---|---|---|
| **PI 板包**（含 SD 写） | `Kernel/HAL/Arm64/Board/rpi-3b/**` | **你** |
| **PI 的 WiFi 复用资料** | `Source/PI/wifi/**` | 你 |
| **公共文件系统自检** | `Kernel/Services/FileSystem/**` | 我 |
| **公共 WPA2 算法** | `Kernel/Services/Wifi/**` | 我 |
| **DUO 板包** | `Kernel/HAL/RiscV/Board/milk-v-duo-s/**` | 我（含 ksend、WiFi） |

**`Kernel/Services/Shell/Commands/Commands.c` 是共享注册口**：已有两处被占（ksend、GUI 探针）。
加新命令**优先在各命令自己的文件里用弱引用注册**，必须改它时只加 1 个声明 + 2 处注册。

---

## 5 · 一句话

> **PI 现在缺的是"块写实现"，不是"块写验证"**；验收命令我这边统一提供（`fs selftest`）。
> **WPA2 请勿复制**，公共模块落地后我会在这里更新一条「可直接调用」的说明。
