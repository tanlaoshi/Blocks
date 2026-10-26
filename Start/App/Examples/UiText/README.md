# UiText — 布局文件模板

`.uitxt` → `uitxt2h` → `MyAppUi.h` → `Build/User/out/MYAPP/MYAPP.ELF`。运行时**不**解析布局。  
须在 Blocks 树内编（`BlocksUser.mk` + `Layout.c`）。

规范：[`Archive/开发/布局文件格式.md`](../../../../Archive/开发/布局文件格式.md)。

## 用法

1. 改 `MyApp.uitxt`  
2. `make -C Start/App/Examples/UiText`  
3. 在 `UiActions.c` 填 `OnId*`  
4. 部署见 [`../../Deploy.md`](../../Deploy.md)

## 注意

- 不改 `Ui.h` ABI；`UiLoadWindow` 只在本模板  
- 按钮物理 id 现为 0..3；checkbox/textbox 上限见 `Ui.h`  
