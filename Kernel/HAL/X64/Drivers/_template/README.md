# Blocks 驱动模板

> **规范**：新设备落在 `Drivers/<层>/<Device>/`，禁止新增 `Drivers/` 根级业务 `.c`。  
> 指南：[`Documents/手册/驱动/驱动开发指南.md`](../../../../Documents/手册/驱动/驱动开发指南.md)。

## 写一个驱动（5 步）

1. 复制 `Template.c` 到 `../<层>/<MyDevice>/MyDriver.c`（**不要**留在 `_template/`；**不要**放到 `Drivers/` 根）
2. 改 `Name`、符号前缀（`Template*` / `gTemplate*`）、`MyDriverRegister` 函数名
3. 实现 `Probe`：发现硬件则 `return 0`，否则 `return -1`
4. 在 `Include/Driver/DriverBoot.h` 增加 `DRIVER_BOOT_*` 序值；在驱动末尾  
   `DRIVER_BOOT(DRIVER_BOOT_MY, MyDriverRegister);`  
   若目录是新夹：在顶层 `Makefile` 的 `DRIVER_SRCS` 增加对应 `wildcard`
5. `cd Kernel && ./build.sh`，再 `cd ../Runtime && ./run.sh`；Shell `lsdev` 应看到 `my-driver …`

## 三种 Probe 匹配模式

详见 [`驱动匹配规范.md`](../../../../Documents/手册/驱动/驱动匹配规范.md)。短记：

| 模式 | 做法 |
| --- | --- |
| A 固定口 / MMIO | Probe 内直接访问 |
| B PCI 填表 | `DRIVER_MATCH` + `BusCtx`（`DEVICE_NODE *`） |
| C 软条件 | VMM / 策略位；真正挂 Backend 可延后 |

## 三种 Backend 挂载

### Input（键盘鼠标）

`Bind` 中：`DriverInputAttach(&gMyBackend)`  
PS/2 → `Input/Ps2/`。USB 键鼠 → `Input/UsbHid/`。

### Block（硬盘）

`Bind` 中：`DriverBlockAttach(&gMyBackend)`  
→ `Block/Ahci/` / `Block/Nvme/` / `Block/Ata/`。

### Net（网卡）

**新网卡**：实现 `NIC_L2`（`SendFrame`/`Poll`/`GetMac`），Bind 里 **`NetAttachNic` / `NetAttachNicKind`**；RX 调 `NetInputFrame`。  
**不要**自造整份 `NET_BACKEND`。  
范例：`../Model/E1000/`。注释骨架：同目录 `TemplateNetL2.c`（**勿**在 `_template/` 内当真驱动改）。  
详见指南 §6.1。

（历史）仅协议门面：`DriverNetAttach` — 一般不由新人网卡驱动调用。

## 常见错误

- **Probe 返回 0 但 lsdev 看不到**：检查 Bind 是否返回 0
- **Register 了但 lsdev 看不到**：忘了 `DRIVER_BOOT`，或未进 `DRIVER_SRCS`
- **`_template/` 里的 .c 不编译**：模板永不编入，必须复制到设备目录
- **新目录编不进**：忘了 Makefile `wildcard`
- **丢在 `Drivers/` 根**：违规；勿再增根级业务 `.c`
- **系统卡死**：Probe 中有死循环；加超时
- **网卡 lsdev 有但不通**：未 `NetAttachNic*`，或 RX 未 `NetInputFrame`

## 禁止

- 把 `_template/Template.c` 留在 `_template/` 下指望它自动编入
- 修改 `_template/` 内的文件当作真驱动用
- **新增** `HAL/*/Drivers/*.c` 根级业务实现（`DemoDriver.c` 遗留除外）
- 让 Probe 中的硬件访问死循环
- 新网卡在 `Network/` 加 `if (gMyNic)` 特例

## Demo / Serial

| 文件 | 处置 |
| ---- | ---- |
| `DemoDriver.c` | 根扁平课堂开关；`DEMO_DRIVER=0` 可关 |
| `Serial/` | COM1 在 `Drivers/Serial/`；USB 转串口在 `Bus/XHCI/Serial/` 等 |

## 进阶

- 简单参考：`Input/Ps2/`
- 复杂参考：`Bus/XHCI/`
- 网卡 L2：`Model/E1000/` + `Include/Driver/DriverNic.h`
- 网卡骨架：同目录 `TemplateNetL2.c`（注释草稿，不链入）
- 完整指南：`Documents/手册/驱动/驱动开发指南.md`
