/*
 * ShellCmdDb.c — K46：dbget / dbset（不堆进 ShellCmd.c）
 *
 * 【初学者】
 * 注册进命令表；实现只调 Db*。可读性盘点要求新命令另文件。
 */
#include "ShellCmd.h"
#include "Db.h"
#include "HalSerial.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

/*
 * CmdDbGet — 打印键值
 * 谁调用：Shell 行 `dbget <key>`（经 ShellCmdRunLine）。
 * 前后文：前 — DbInitialize（懒）；兄弟 CmdDbSet
 */
static void CmdDbGet(int Argc, char **Argv) {
    char Val[DB_VAL_MAX];
    int Err;

    if (Argc < 2) {
        Put("usage: dbget <key>\n");
        return;
    }
    Err = DbGet(Argv[1], Val, sizeof(Val));
    if (Err == DB_NOENT) {
        Put("dbget: not found\n");
        return;
    }
    if (Err != DB_OK) {
        Put("dbget: error\n");
        return;
    }
    Put(Val);
    Put("\n");
}

/*
 * CmdDbSet — 写键值（多参数空格拼接）
 * 谁调用：Shell `dbset <key> <value...>`。
 * 前后文：后 — 盘上 BLOCKS.DB；验收再 dbget / 复开
 */
static void CmdDbSet(int Argc, char **Argv) {
    char Val[DB_VAL_MAX];
    int o = 0;
    int i;
    int j;
    int Err;

    if (Argc < 3) {
        Put("usage: dbset <key> <value...>\n");
        return;
    }
    for (i = 2; i < Argc; i++) {
        if (i > 2 && o + 1 < (int)sizeof(Val)) {
            Val[o++] = ' ';
        }
        for (j = 0; Argv[i][j] && o + 1 < (int)sizeof(Val); j++) {
            Val[o++] = Argv[i][j];
        }
    }
    Val[o] = 0;
    Err = DbSet(Argv[1], Val);
    if (Err != DB_OK) {
        Put("dbset: fail\n");
        return;
    }
    Put("dbset: ok\n");
}

/*
 * ShellCmdDbRegister — 挂 dbget/dbset
 * 谁调用：ShellCmdInitialize。
 * 前后文：前 — ShellCmdRegister 已可用；兄弟 ShellSysRegister
 */
void ShellCmdDbRegister(void) {
    ShellCmdRegister("dbget", "get BLOCKS.DB value", CmdDbGet);
    ShellCmdRegister("dbset", "set BLOCKS.DB key value", CmdDbSet);
}
