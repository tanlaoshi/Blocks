/*
 * PR-A7 占位：无嵌入时满足 ProcessRunDemo 链接。
 * 现 Arm/RiscV 与 x86 同：Makefile 链 User_hello_blob.o，并 filter 掉本文件。
 * 保留源文件仅作文档；勿再编进 Kernel.elf。
 */
char _binary_User_hello_elf_start;
char _binary_User_hello_elf_end;
