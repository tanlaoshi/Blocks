# Store — 离线商店货架（Image 阶段 · store-src）

**真源**：本目录（`ToyImage/Store/`）。**不是** ToyKernel Code 层；商店逻辑在 `ToyKernel/CodeD-Services/Store/`。

Guest 卷根同样是 **`Store/`**（`prepare-rootfs` / `build.sh` 同步）。废止 `Assets/Store/`、`StoreCache/`。

| 路径 | 用途 |
|------|------|
| `Store/catalog.txt` | 可安装项列表 |
| `Store/packages/<id>/` | `PKG.TXT` + 载荷（ELF / 字体 / 资源） |
| `Apps/`（Guest） | 已安装 `type=app` |
| `Assets/Fonts/`（Guest） | 已安装 `type=font` |
| `Assets/Packs/`（Guest） | 已安装 `type=asset` |

`packages/chat/` 与其它 app 一样进 Guest catalog（离线 `store install chat`）；LAN 导出仍可用。

## catalog.txt

`#` 注释；字段 `|` 分隔：

```text
id|type|version|file|sha256|arch|title
```

`type` = `app` \| `font` \| `asset`。详情见 Kernel 文档 [`局域网商店与聊天.md`](../ToyKernel/Archive/开发/局域网商店与聊天.md)。
