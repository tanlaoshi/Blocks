/*
 * freestanding FTSTDLIB：无 FILE/malloc；字符串走内核 CString；setjmp 自备。
 */
#ifndef FTSTDLIB_H_
#define FTSTDLIB_H_

#include <stddef.h>
#include <limits.h>

#define ft_ptrdiff_t  long

#define FT_CHAR_BIT    CHAR_BIT
#define FT_USHORT_MAX  USHRT_MAX
#define FT_INT_MAX     INT_MAX
#define FT_INT_MIN     INT_MIN
#define FT_UINT_MAX    UINT_MAX
#define FT_LONG_MIN    LONG_MIN
#define FT_LONG_MAX    LONG_MAX
#define FT_ULONG_MAX   ULONG_MAX
#ifdef LLONG_MAX
#define FT_LLONG_MAX   LLONG_MAX
#endif
#ifdef LLONG_MIN
#define FT_LLONG_MIN   LLONG_MIN
#endif
#ifdef ULLONG_MAX
#define FT_ULLONG_MAX  ULLONG_MAX
#endif

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strrchr(const char *s, int c);
char *strstr(const char *hay, const char *needle);

#define ft_memchr   memchr
#define ft_memcmp   memcmp
#define ft_memcpy   memcpy
#define ft_memmove  memmove
#define ft_memset   memset
#define ft_strcat   strcat
#define ft_strcmp   strcmp
#define ft_strcpy   strcpy
#define ft_strlen   strlen
#define ft_strncmp  strncmp
#define ft_strncpy  strncpy
#define ft_strrchr  strrchr
#define ft_strstr   strstr

/* 无 FILE：DISABLE_STREAM 后不应触达；桩掉类型即可 */
typedef struct FT_DummyFile_ {
  int unused;
} FT_DummyFile;
#define FT_FILE      FT_DummyFile
#define ft_fclose(x) ((void)(x))
#define ft_fopen(p, m) ((FT_FILE *)0)
#define ft_fread(b, s, n, f) ((size_t)0)
#define ft_fseek(f, o, w) ((int)-1)
#define ft_ftell(f) ((long)0)
int FtSnprintf(char *buf, size_t n, const char *fmt, ...);
#define ft_snprintf FtSnprintf

void FtQsort(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *));
#define ft_qsort FtQsort

#define ft_scalloc(n, s) ((void *)0)
#define ft_sfree(p) ((void)(p))
#define ft_smalloc(s) ((void *)0)
#define ft_srealloc(p, s) ((void *)0)

long strtol(const char *nptr, char **endptr, int base);
#define ft_strtol strtol
#define ft_getenv(n) ((char *)0)

#include "setjmp.h"
#define ft_jmp_buf     jmp_buf
#define ft_longjmp     longjmp
#define ft_setjmp(b)   setjmp(*(ft_jmp_buf *)&(b))

#include <stdarg.h>

#endif
