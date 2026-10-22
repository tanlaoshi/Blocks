/*
 * CString.h — 整串相等，以及带上限、保证结尾 0 的拷贝。
 * 空指针比较视为不相等。空来源写成空串。
 */
#ifndef CSTRING_H
#define CSTRING_H

#include "BootTypes.h"

int StringEqual(const char *A, const char *B);
void StringCopy(char *Dst, UINTN Max, const char *Src);

/* freestanding CRT（Library/CString.c）；业务走这些声明，不直调编译器 intrinsic */
void *memcpy(void *Dst, const void *Src, UINTN Len);
void *memset(void *Dst, int Val, UINTN Len);
void *memmove(void *Dst, const void *Src, UINTN Len);
int memcmp(const void *A, const void *B, UINTN Len);
UINTN strlen(const char *S);

#endif
