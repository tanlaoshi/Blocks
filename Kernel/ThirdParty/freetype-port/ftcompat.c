/*
 * FreeType freestanding 兼容：qsort / 缺的 str* / snprintf 桩。
 */
#include <stddef.h>
#include <stdarg.h>

void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s;
  unsigned char ch = (unsigned char)c;
  size_t i;

  for (i = 0; i < n; i++) {
    if (p[i] == ch) {
      return (void *)(p + i);
    }
  }
  return 0;
}

int strcmp(const char *a, const char *b) {
  if (!a || !b) {
    return (a != b);
  }
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src) {
  char *d = dst;

  if (!dst) {
    return dst;
  }
  if (!src) {
    *dst = 0;
    return dst;
  }
  while ((*d++ = *src++) != 0) {
  }
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i;

  if (!dst) {
    return dst;
  }
  for (i = 0; i < n; i++) {
    if (src && src[i]) {
      dst[i] = src[i];
    } else {
      dst[i] = 0;
      i++;
      for (; i < n; i++) {
        dst[i] = 0;
      }
      break;
    }
  }
  return dst;
}

char *strcat(char *dst, const char *src) {
  char *d = dst;

  if (!dst) {
    return dst;
  }
  while (*d) {
    d++;
  }
  if (src) {
    while ((*d++ = *src++) != 0) {
    }
  } else {
    *d = 0;
  }
  return dst;
}

char *strrchr(const char *s, int c) {
  const char *last = 0;
  unsigned char ch = (unsigned char)c;

  if (!s) {
    return 0;
  }
  while (*s) {
    if ((unsigned char)*s == ch) {
      last = s;
    }
    s++;
  }
  if (ch == 0) {
    return (char *)s;
  }
  return (char *)last;
}

char *strstr(const char *hay, const char *needle) {
  size_t nlen;
  size_t i;

  if (!hay || !needle) {
    return 0;
  }
  if (!*needle) {
    return (char *)hay;
  }
  nlen = 0;
  while (needle[nlen]) {
    nlen++;
  }
  for (; *hay; hay++) {
    for (i = 0; i < nlen; i++) {
      if (hay[i] != needle[i]) {
        break;
      }
    }
    if (i == nlen) {
      return (char *)hay;
    }
  }
  return 0;
}

static void FtSwapBytes(unsigned char *a, unsigned char *b, size_t size) {
  size_t i;
  unsigned char t;

  for (i = 0; i < size; i++) {
    t = a[i];
    a[i] = b[i];
    b[i] = t;
  }
}

void FtQsort(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *)) {
  size_t i;
  size_t j;
  unsigned char *b = (unsigned char *)base;

  if (!base || !compar || size == 0 || nmemb < 2) {
    return;
  }
  /* 插入排序：nmemb 小（cmap 分段等） */
  for (i = 1; i < nmemb; i++) {
    for (j = i; j > 0; j--) {
      unsigned char *p = b + (j - 1) * size;
      unsigned char *q = b + j * size;

      if (compar(p, q) <= 0) {
        break;
      }
      FtSwapBytes(p, q, size);
    }
  }
}

int FtSnprintf(char *buf, size_t n, const char *fmt, ...) {
  (void)fmt;
  if (buf && n > 0) {
    buf[0] = 0;
  }
  return 0;
}
