/*
 * DataBase.c — K46：dbget / dbset（不堆进 ShellCommand.c）
 *
 * 【初学者】
 * 注册进命令表；实现只调 DataBase*。命令字保持 dbget/dbset（课上手短）。
 */
#include "ShellCommand.h"
#include "DataBase.h"
#include "HalSerial.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

/*
 * CommandDataBaseGet — 打印键值
 * 谁调用：Shell 行 `dbget <key>`（经 ShellCommandRunLine）。
 * 前后文：前 — DataBaseInitialize（懒）；兄弟 CommandDataBaseSet
 */
static void CommandDataBaseGet(int Argc, char **Argv) {
    char Value[DATA_BASE_VALUE_MAX];
    int Error;

    if (Argc < 2) {
        Put("usage: dbget <key>\n");
        return;
    }
    Error = DataBaseGet(Argv[1], Value, sizeof(Value));
    if (Error == DATA_BASE_NOENT) {
        Put("dbget: not found\n");
        return;
    }
    if (Error != DATA_BASE_OK) {
        Put("dbget: error\n");
        return;
    }
    Put(Value);
    Put("\n");
}

/*
 * CommandDataBaseSet — 写键值（多参数空格拼接）
 * 谁调用：Shell `dbset <key> <value...>`。
 * 前后文：后 — 盘上 BLOCKS.DB；验收再 dbget / 复开
 */
static void CommandDataBaseSet(int Argc, char **Argv) {
    char Value[DATA_BASE_VALUE_MAX];
    int o = 0;
    int i;
    int j;
    int Error;

    if (Argc < 3) {
        Put("usage: dbset <key> <value...>\n");
        return;
    }
    for (i = 2; i < Argc; i++) {
        if (i > 2 && o + 1 < (int)sizeof(Value)) {
            Value[o++] = ' ';
        }
        for (j = 0; Argv[i][j] && o + 1 < (int)sizeof(Value); j++) {
            Value[o++] = Argv[i][j];
        }
    }
    Value[o] = 0;
    Error = DataBaseSet(Argv[1], Value);
    if (Error != DATA_BASE_OK) {
        Put("dbset: fail\n");
        return;
    }
    Put("dbset: ok\n");
}

/*
 * DataBaseRegister — 挂 dbget/dbset
 * 谁调用：ShellCommandInitialize。
 */
void DataBaseRegister(void) {
    ShellCommandRegister("dbget", "get BLOCKS.DB value", CommandDataBaseGet);
    ShellCommandRegister("dbset", "set BLOCKS.DB key value", CommandDataBaseSet);
}
