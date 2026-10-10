/*
 * DataBase.h — 轻量文本 KV（K46；盘文件 BLOCKS.DB，对标现网 DB1）
 *
 * 【初学者】
 * 盘上默认卷文件 BLOCKS.DB：每行 key=value，# 开头为注释。
 * Shell：dbget / dbset（命令字保留短名；代码标识符用 DataBase*）。
 */
#ifndef DATA_BASE_H
#define DATA_BASE_H

#include "BootTypes.h"

#define DATA_BASE_PATH        "BLOCKS.DB"
#define DATA_BASE_KEY_MAX     48
#define DATA_BASE_VALUE_MAX   64
#define DATA_BASE_MAX_RECORDS 64

#define DATA_BASE_OK     0
#define DATA_BASE_ERR   (-1)
#define DATA_BASE_NOENT (-2)
#define DATA_BASE_FULL  (-3)
#define DATA_BASE_INVAL (-4)

/*
 * DataBaseInitialize — 读盘或建空库，可重复调用
 * 谁调用：FileSystemInitialize（挂卷成功后）；Shell 命令前亦幂等。
 * 前后文：前 — VolumeMountAll；后 — DataBaseGet/DataBaseSet；兄弟 — ThemeConfiguration（仍独立 THEME.CFG）
 */
int DataBaseInitialize(void);

/*
 * DataBaseGet — 按键取字符串
 * 谁调用：Shell dbget；后刀 Theme/Desktop。
 * 返回：DATA_BASE_OK / DATA_BASE_NOENT / DATA_BASE_INVAL
 */
int DataBaseGet(const char *Key, char *Out, UINTN OutMax);

/*
 * DataBaseSet — 写入内存并立即刷盘（Value 空则删键）
 * 谁调用：Shell dbset。
 * 前后文：兄弟 DataBaseGet；落盘 DataBaseSave（本模块内）
 */
int DataBaseSet(const char *Key, const char *Value);

#endif
