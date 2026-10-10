# Blocks 命名规范盘点

> 对照：`~/tanlaoshi/edk2/ToyKernel/Documents/开发/开发命名规范.md`  
> 范围：`Kernel/`（不含 `ThirdParty/`、`Build/`）

---

## 钉死：目录即命名空间（2026-10-10）

### 条文

1. **已分到子目录的模块**，目录下的**文件名、函数名、类型名不再重复该目录名**。  
2. **与目录同名的主文件不改**——它是该模块入口 / 编制流程枢纽（例：`FileSystem/FileSystem.c`、`Network/Network.c`、`Gui/Gui.c`、`Serial/Serial.c`）。  
3. **文件名不怕长、禁止缩写**；不合适由你改口令。  
4. **编译产物（方案 3）**：`Build/<Arch>/Core/<子目录>/Name.o`、`Build/<Arch>/Hal/<Arch|Common>/…`，镜像源码树，跨目录同名不冲突。

### 示例

| 目录 | 正确 | 错误 |
| ---- | ---- | ---- |
| `FileSystem/` | `Volume.c` · `VolumeMountAll` | `FileSystemVolume*` |
| `Network/` | `Ping.c` · `Ping` / `TcpInitialize` | `NetworkPing` / `NetworkTcpInitialize` |
| `Gui/` | `Desktop.c` · `DesktopPaintIcons` | `GuiDesktop*` |
| `Gui/` 主文件 | `Gui.c` · `GuiInitialize` | （主文件保留目录名） |
| `Hal/` | 见下节例外 | 把 `HalSerialWrite` 改成 `SerialWrite`（会与 Core 撞名） |

### Hal 例外（层前缀）

`Hal*` 是**分层前缀**（现网《开发命名规范》§2.2），不是「目录名重复」：

- **文件 / 符号保留 `Hal*`**（`Hal/X64/HalSerial.c`、`HalSerialWrite`）。  
- 若去掉 `Hal`，会与 Core 的 `SerialInitialize` / `VideoInitialize` 等**链接撞名**。  
- 产物仍按目录：`Build/X64/Hal/X64/HalSerial.o`。

---

## 已按条文改完的目录

| 目录 | 主文件（保留） | 子文件（去目录前缀 / 写全） |
| ---- | -------------- | --------------------------- |
| `FileSystem/` | `FileSystem.c` | `Volume` `FatVolume` `FatDirectory` `FatSlot` `FatWrite` `FatMakeDirectory` `FatDelete` `FatRename` `FatAllocate` `DataBase` `Store*` … |
| `Network/` | `Network.c` | `Ip` `Ping` `Udp` `Tcp` `Lwip` `Configuration` |
| `Gui/` | `Gui.c` | `Layout` `Desktop` `Settings` `Files` `Start` `Window` `WindowPaint` `Cursor` `Pointer` |
| `Console/` | `Console.c` | `ShellSystem` `ElfLoader` `Process`；命令见下 `ShellCommand/` |
| `Console/ShellCommand/` | `ShellCommand.c` | `FileSystem` `Theme` `Network` `NetworkTcp` `NetworkUdp` `DataBase` `Store`（不叠 `ShellCommand`） |
| 其它 | `Serial` `Memory` `Cpu` `USB` `Scheduler` `Video` `Driver`… | 子文件本就未叠目录名或已写全 |

---

## 仍待

| 项 | 说明 |
| -- | ---- |
| Hal `*Init`→`*Initialize` | PR-B-name-1 |
| 局部 `Buf`/`Len`/… | 改到文件时顺手清 |
| `ShellCommand/` 子文件 Register 与 Core 同名模块并存 | 已用 `FileSystemRegister` 等；若撞链再加区分词（勿叠回夹名） |
| Hal 一设备一夹 | 钉条 #5；迁驱动时做 |

---

## 策略

新符号必须遵守本文「目录即命名空间」+ 现网全词 PascalCase；存量点名 JX，不一次翻库狂欢。
