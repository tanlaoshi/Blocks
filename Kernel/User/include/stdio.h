#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define EOF (-1)

typedef struct ToyFile FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int puts(const char *s);
int putchar(int c);
void perror(const char *s);
int fprintf(FILE *fp, const char *fmt, ...);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int fputc(int c, FILE *fp);
int fputs(const char *s, FILE *fp);
int fgetc(FILE *fp);
int getchar(void);
char *fgets(char *s, int size, FILE *fp);
int sscanf(const char *str, const char *fmt, ...);
int scanf(const char *fmt, ...);
int fscanf(FILE *fp, const char *fmt, ...);

/* fopen("w") 不截断（内核忽略 O_TRUNC）；写侧行缓冲，见 fflush/ungetc */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
int fflush(FILE *fp);
int ungetc(int c, FILE *fp);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);
int fseek(FILE *fp, long offset, int whence);
long ftell(FILE *fp);
int feof(FILE *fp);
int ferror(FILE *fp);
int fileno(FILE *fp);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);

#endif
