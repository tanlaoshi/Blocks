/*
 * FileSystemInit.c — InitializeFileSystem（开机模块编织）
 *
 * 默认策略：同夹 Fat/（FS=fat）；门面仍在 Services/FileSystem（阶段 B 再并）。
 */
#include "KernelModulesPrivate.h"
#include "FileSystem.h"

int InitializeFileSystem(void) {
    return FileSystemInitialize();
}
