# UiDesigner 包模板

1. PC：`python3 Kernel/Tools/UiDesigner/designer.py` → 导出 `MyApp.uitxt`  
2. 覆盖本目录 `MyApp.uitxt`  
3. `make -C Bingo/App/Examples/UiDesigner` → `Build/User/out/MYAPP/MYAPP.ELF`  
4. 填 `UiActions.c`  
5. 部署见 [`../../Deploy.md`](../../Deploy.md)

依赖：`uitxt2h`、`User/include/UiLayout.h`、`User/Library/Ui/Layout.c`（树内）。  
说明：[`../../UI/拖控件设计器.md`](../../UI/拖控件设计器.md)。
