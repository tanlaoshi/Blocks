# Bingo / App — 应用开发

> 静态链接 ELF → Guest `exec` →（可选）商店目录包。  
> **动手**：[`Template/`](Template/) · [`Examples/`](Examples/) · [`Deploy.md`](Deploy.md)。

| 文档 | 内容 |
| ---- | ---- |
| [`红线.md`](红线.md) | 三条别踩 + PKG 认窗 + 库分层 |
| [`身份.md`](身份.md) | 系统身份、SDK 三层 |
| [`Guide.md`](Guide.md) | 上手：CRT / UI / Net / 打包 |
| [`API.md`](API.md) | 一页 API 表 |
| [`ABI.md`](ABI.md) | 开课冻结：承诺 / 不承诺 |
| [`LibcGaps.md`](LibcGaps.md) | ISO C 加厚缺口 |
| [`UI/`](UI/) | 布局文件、多页面、拖控件设计器 |

**工具链**：`cd Kernel && ./Tools/build-sdk.sh` → `Build/BlocksSDK`；离树 `make BLOCKS_SDK=…`。  
**课设 / Class / Store**：[`Kernel/User/Apps/README.md`](../../Kernel/User/Apps/README.md)。  
**上机**：[`../BINGO.md`](../BINGO.md)。编码规矩：[`../Contribute/Coding.md`](../Contribute/Coding.md)。
