/*
 * Pkg/main.c — 课外用户程序模板示例（PR-L1）
 * 构建：在 User/Package 下 make（或 make -C User/Package）
 * 产物：MYAPP.ELF → 拷到 ToyImage/rootfs/ 后 exec MYAPP.ELF
 */
#include <stdio.h>
#include <Blocks/version.h>

int main(void) {
    printf("Blocks pkg template (CRT %s)\n", CRT_VERSION_STRING);
    return 0;
}
