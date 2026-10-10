/*
 * Store.h — 本地商店薄层（K47：读清单 + 装一包）
 *
 * 【初学者】
 * 对标现网 Store/S-job 的最小子集：盘上 STORE.CAT 清单 + 把包文件拷到安装名 + BLOCKS.DB 登记。
 * 本刀不做商店 UI / 作业互斥 / 网络拉包（K48+）。
 *
 * 【路径】当前 Fat 只认根目录 8.3，故清单叫 STORE.CAT（不是 Store/catalog.txt）。
 */
#ifndef STORE_H
#define STORE_H

#include "BootTypes.h"

#define STORE_CATALOG_PATH "STORE.CAT"
#define STORE_ID_MAX       32
#define STORE_FILE_MAX     13 /* 8.3 文本名 */
#define STORE_TITLE_MAX    48
#define STORE_ARCH_MAX     16
#define STORE_ENTRIES_MAX  16

#define STORE_OK     0
#define STORE_ERR   (-1)
#define STORE_NOENT (-2)
#define STORE_INVAL (-3)
#define STORE_NOSPC (-4)

typedef struct STORE_ENTRY {
    char Id[STORE_ID_MAX];
    char Type[12]; /* app / asset / font … */
    UINT32 Version;
    char File[STORE_FILE_MAX];
    char Arch[STORE_ARCH_MAX];
    char Title[STORE_TITLE_MAX];
} STORE_ENTRY;

/*
 * StoreLoadCatalog — 读根目录 STORE.CAT（| 分隔）；缺文件则内置一条 demopack
 * 谁调用：Shell store list / StoreInstall。
 * 返回：STORE_OK；*OutCount 条数
 */
int StoreLoadCatalog(STORE_ENTRY *Out, int Max, int *OutCount);

/*
 * StoreInstall — 按 id 装一包：源=清单 File（根目录），目标=A+id 截断+扩展名；记 si.<id>
 * 谁调用：Shell store install <id>。
 * 前后文：前 — Fat/DataBase 可用；后 — ls 见安装名；兄弟 — StoreLoadCatalog
 */
int StoreInstall(const char *Id);

/* 本机 arch 标签（x86_64 / arm64 / riscv64） */
const char *StoreHostArch(void);

#endif
