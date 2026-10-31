/*
 * Ksend.h — 串口增量写卡门面（Duo S 开发期工具，v1）
 *
 * 人话：不拔卡，PC 只发和卡上不一样的那几块 Kernel.elf，写完重启。
 * 从哪读：Ksend.c（info/selftest）→ KsendTransfer.c（协议）→ KsendFile.c（落盘）。
 * 别改：三个符号由 Services/Shell/Commands/Ksend.c 弱引用；其它 RISC-V 靶没有
 *       这个模块（链成 NULL），x86/arm64 连那段都不编。
 */
#ifndef KSEND_H
#define KSEND_H

/*
 * 只查板上现状：Kernel.elf 的 size / 块数 / 整体 CRC32，一个字节都不写。
 * 返回 0 成功；非 0 失败（失败行同样打在串口上）。
 */
int KsendInfo(void);

/*
 * 自检：在 BLOCKS 卷上建 8 KB 的 KSELFT.BIN（先新建、再覆写两遍），
 * 读回逐字节比对后删除。判的是 FAT 写路径能不能用。
 * 返回 0 成功；非 0 失败。
 */
int KsendSelftest(void);

/*
 * 完整传输：备份 → 发块表 → 收差量块 → flush → 读回校验 →（可选）重启。
 * TargetPath = 0/空串 → 默认 BLOCKS:Kernel.elf；否则写该路径（可带卷名，
 * 也可写裸文件名走默认卷）。前缀 BLOCKS:/BOOT:/ESP:/卷字母/卷下标都可用；
 * BOOT: 指向挂载时探到 boot.sd 的启动分区。
 * 备份名 = 目标全名追加 .bak（boot.sd → boot.sd.bak），写前先落盘并读回校验。
 * Backup=0 时跳过这一步（--no-backup）：仍会读旧内容算差量块，但不写 .bak，
 * 失败也不回滚，串口上明确打 backup: skipped (--no-backup)。
 * Reboot=1 时校验通过才打印 KSEND OK 并复位（不返回）；Reboot=0 打印 OK 后
 * 返回 shell，复位留给操作者（deploy.sh 用）。失败时打印 KSEND FAIL，必要时
 * 用备份恢复，绝不重启。返回 0 成功、1 失败。
 */
int KsendTransfer(const char *TargetPath, int Reboot, int Backup);

#endif /* KSEND_H */
