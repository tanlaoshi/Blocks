/*
 * ProcessPrivate.h — 进程加载内部（仅 Core/Process；User 勿 include）
 */
#ifndef PROCESS_PRIVATE_H
#define PROCESS_PRIVATE_H

#include "Elf.h"
#include "VirtualMemory.h"

#define EXEC_ARGV_MAX 8
#define EXEC_ENV_MAX  8
#define EXEC_ARG_LEN  64

int ProcessStartElf(VIRTUAL_ADDRESS_SPACE *Space, const ELF_LOAD_RESULT *Info,
                    const char *Name, int StdoutSerialOnly);
int ProcessLoadPath(const char *Path, VIRTUAL_ADDRESS_SPACE **OutSpace,
                    ELF_LOAD_RESULT *OutInfo);
void ProcessStopAllUsers(void);
/* PR-S-app-font：Apps/<id>/ + PKG font= → 挂/卸私有字 */
void ProcessApplyAppFont(const char *Path);
void ProcessRestoreAppFont(void);
/* PKG window=yes 才占用户窗。Hello 这类控制台程序不要关掉已经开着的窗。 */
int ProcessAppTakesWindow(const char *Path);
/* argc | argv… | NULL | envp… | NULL | 字符串区 → *OutRsp */
int ProcessSetupArgEnvStack(VIRTUAL_ADDRESS_SPACE *Space, UINT64 StackTop,
                            char ArgBuf[][EXEC_ARG_LEN], int Argc,
                            char EnvBuf[][EXEC_ARG_LEN], int Envc,
                            UINT64 *OutRsp);

#endif

