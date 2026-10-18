/*
 * CString.h — 整串相等，以及带上限、保证结尾 0 的拷贝。
 * 空指针比较视为不相等。空来源写成空串。
 */
#ifndef CSTRING_H
#define CSTRING_H

#include "BootTypes.h"

int StringEqual(const char *A, const char *B);
void StringCopy(char *Dst, UINTN Max, const char *Src);

#endif
