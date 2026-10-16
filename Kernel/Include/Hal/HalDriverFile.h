/*
 * HalDriverFile.h — 驱动读文件槽（不 #include FileSystem）
 *
 * FileSystem 就绪时 HalDriverFileReadRegister；未注册则读失败。
 */
#ifndef HAL_DRIVER_FILE_H
#define HAL_DRIVER_FILE_H

#include "BootTypes.h"

typedef int (*HAL_DRIVER_FILE_READ_FN)(const char *Path, void *Buffer, UINTN MaxSize,
                                      UINTN *OutSize);

void HalDriverFileReadRegister(HAL_DRIVER_FILE_READ_FN Fn);
/* 成功 0；未注册或读失败为负。OutSize 可为 NULL。 */
int HalDriverFileRead(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);

#endif
