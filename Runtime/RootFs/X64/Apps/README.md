# Apps/ — 已安装应用（目录包）

Guest 卷根：`Apps/`。

## 正统布局（PR-S-bundle）

```text
Apps/<id>/
  <FILE>.ELF     # 与 PKG file= 同名
  PKG.TXT        # 安装时从包目录拷入
  Assets/        # 可选：图标、私有字体、数据
```

- 来源：`store install` / `store combo`（目录包 `Store/packages/<id>/` + `catalog.txt`）。
- 运行：`exec Apps/<id>/<FILE>.ELF`；菜单/桌面看 PKG 的 `taskbar=` / `desktop=`。
- 卸装：`store remove` / `uncombo` → 删整树 `Apps/<id>/`。

## 遗留（勿新增）

历史上曾有扁平 `Apps/HELLO.ELF`、`Apps/TASKMGR.ELF`。新应用**禁止**再往 `Apps/` 根丢 ELF。

开发与打包：[`Bingo/App/Guide.md`](../../../../Bingo/App/Guide.md) · [`Bingo/App/Deploy.md`](../../../../Bingo/App/Deploy.md)。商店说明：[`../Store/README.md`](../Store/README.md)。

勿把手动构建垃圾提交进本目录。
