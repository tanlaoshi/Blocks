/*
 * Db.h — 轻量文本 KV（K46；盘文件 BLOCKS.DB，对标现网 DB1）
 *
 * 【初学者】
 * 盘上默认卷文件 BLOCKS.DB：每行 key=value，# 开头为注释。
 * Shell：dbget / dbset。完整 Theme/Store 写库后刀再接。
 */
#ifndef DB_H
#define DB_H

#include "BootTypes.h"

#define DB_PATH        "BLOCKS.DB"
#define DB_KEY_MAX     48
#define DB_VAL_MAX     64
#define DB_MAX_RECORDS 64

#define DB_OK     0
#define DB_ERR   (-1)
#define DB_NOENT (-2)
#define DB_FULL  (-3)
#define DB_INVAL (-4)

/*
 * DbInitialize — 读盘或建空库，可重复调用
 * 谁调用：FileSystemInitialize（挂卷成功后）；Shell 命令前亦幂等。
 * 前后文：前 — FsVolMountAll；后 — DbGet/DbSet；兄弟 — ThemeCfg（仍独立 THEME.CFG）
 */
int DbInitialize(void);

/*
 * DbGet — 按键取字符串
 * 谁调用：Shell dbget；后刀 Theme/Desktop。
 * 返回：DB_OK / DB_NOENT / DB_INVAL
 */
int DbGet(const char *Key, char *Out, UINTN OutMax);

/*
 * DbSet — 写入内存并立即刷盘（Value 空则删键）
 * 谁调用：Shell dbset。
 * 前后文：兄弟 DbGet；落盘 DbSave（本模块内）
 */
int DbSet(const char *Key, const char *Value);

#endif
