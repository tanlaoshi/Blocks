#include <Blocks/File.h>
/*
 * WriteFile.c — CRT2：写 USERNOTE.TXT（WRITE.ELF）
 * 旧 int 0x80 汇编见 WriteFile.S
 */
#include "errno.h"
#include "fcntl.h"
#include "stdio.h"
#include "unistd.h"

int main(void) {
    static const char msg[] = "from ring3!\n";
    int fd;
    ssize_t n;

    fd = FileOpen("USERNOTE.TXT");
    if (fd < 0) {
        printf("write: open failed errno=%d\n", errno);
        return 1;
    }
    n = FileWrite(fd, msg, sizeof(msg) - 1);
    if (n < 0) {
        printf("write: write failed errno=%d\n", errno);
        FileClose(fd);
        return 1;
    }
    if (FileClose(fd) < 0) {
        printf("write: close failed errno=%d\n", errno);
        return 1;
    }
    printf("ok\n");
    return 0;
}
