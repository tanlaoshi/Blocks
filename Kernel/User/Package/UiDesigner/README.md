# UiDesigner 包模板

1. PC：`python3 Tools/UiDesigner/designer.py` → 导出 `MyApp.uitxt`  
2. 覆盖本目录 `MyApp.uitxt`  
3. `make` → `build/MYAPP.ELF`  
4. 填 `UiActions.c`  
5. 拷到 RootFs 后 `exec MYAPP.ELF`  

依赖：`uitxt2h`、`User/include/UiLayout.h`、`User/Library/Ui/Layout.c`。  
说明：[`Documents/手册/应用UI/拖控件设计器.md`](../../../../Documents/手册/应用UI/拖控件设计器.md)。
