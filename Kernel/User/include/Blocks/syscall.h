/*
 * Blocks/syscall.h — 系统调用号与薄封装（PR-L1；原 blocks_syscall.h）
 * 与内核 Include/Syscall.h 编号一致；用户程序勿包含内核头。
 * 汇编（User/Apps 下 .S）可 include 本头只取 SYS_*（见 __ASSEMBLER__）。
 */
#ifndef BLOCKS_SYSCALL_H
#define BLOCKS_SYSCALL_H

/* SYS_* 号段见 SyscallABI.h（本位宏名；号值稳定；SDK 同用 <Abi/…>） */
#include <Abi/SyscallABI.h>

#ifndef __ASSEMBLER__

#include <sys/types.h>
#include <Blocks/version.h>
#include <sched.h>

long blocks_syscall(long n, long a, long b, long c);
/* 与 blocks_syscall 同一条指令；第二返回值在 rdx / x1 / a1（wait 退出码） */
typedef struct {
    long A;
    long B;
} BLOCKS_RET2;
BLOCKS_RET2 blocks_syscall2(long n, long a, long b, long c);

static inline long blocks_exit(long status) {
    return blocks_syscall(SYS_PROCESS_EXIT, status, 0, 0);
}

static inline ssize_t blocks_write(long fd, const void *buf, size_t len) {
    return (ssize_t)blocks_syscall(SYS_FILE_WRITE, fd, (long)buf, (long)len);
}

static inline long blocks_open(const char *path) {
    return blocks_syscall(SYS_FILE_OPEN, (long)path, 0, 0);
}

static inline ssize_t blocks_read(long fd, void *buf, size_t len) {
    return (ssize_t)blocks_syscall(SYS_FILE_READ, fd, (long)buf, (long)len);
}

static inline long blocks_close(long fd) {
    return blocks_syscall(SYS_FILE_CLOSE, fd, 0, 0);
}

static inline long blocks_lseek(long fd, long offset, long whence) {
    return blocks_syscall(SYS_FILE_SEEK, fd, offset, whence);
}

/* PR-U-sched-yield：实现迁至 User/crt/sched.c 的 sched_yield；保留旧名为宏别名 */
#define blocks_yield() sched_yield()

static inline long blocks_execve(const char *path, char *const argv[],
                              char *const envp[]) {
    return blocks_syscall(SYS_PROCESS_EXEC, (long)path, (long)argv, (long)envp);
}

static inline long blocks_fork(void) {
    return blocks_syscall(SYS_PROCESS_FORK, 0, 0, 0);
}

static inline long blocks_wait(long options) {
    return blocks_syscall(SYS_PROCESS_WAIT, options, 0, 0);
}

static inline long blocks_pipe(int pipefd[2]) {
    return blocks_syscall(SYS_PROCESS_PIPE, (long)pipefd, 0, 0);
}

static inline long blocks_dup(int fd) {
    return blocks_syscall(SYS_FILE_DUP, fd, 0, 0);
}

static inline long blocks_brk(long addr) {
    return blocks_syscall(SYS_MEMORY_BRK, addr, 0, 0);
}

static inline long blocks_kill(long pid, long sig) {
    return blocks_syscall(SYS_PROCESS_KILL, pid, sig, 0);
}

static inline long blocks_create_window(const char *title, long w, long h) {
    return blocks_syscall(SYS_WINDOW_CREATE, (long)title, w, h);
}

static inline long blocks_damage(long wid, const char *text) {
    return blocks_syscall(SYS_WINDOW_DAMAGE, wid, (long)text, 0);
}

static inline long blocks_damage_rect(long wid, const void *desc) {
    return blocks_syscall(SYS_GRAPHICS_DAMAGE_RECT, wid, (long)desc, 0);
}

static inline long blocks_poll_input(long wid) {
    return blocks_syscall(SYS_WINDOW_POLL, wid, 0, 0);
}

static inline long blocks_ui_button(long wid, long button_id, const char *label) {
    return blocks_syscall(SYS_UI_BUTTON, wid, button_id, (long)label);
}

/* PR-UID-screen：查屏宽/高，写两个用户 int；0=成功 */
static inline long blocks_screen_size(int *out_w, int *out_h) {
    return blocks_syscall(SYS_UI_SCREEN_SIZE, (long)out_w, (long)out_h, 0);
}

/* PR-UID-font：公开换字 API（薄封装；改全局当前字）。0=成功 */
static inline long blocks_set_font_id(long id) {
    return blocks_syscall(SYS_UI_SET_FONT_ID, id, 0, 0);
}

/* 返回已注册字体数（<0=非用户/失败） */
static inline long blocks_font_count(void) {
    return blocks_syscall(SYS_UI_FONT_COUNT, 0, 0, 0);
}

static inline long blocks_file_stat(const char *path, void *out) {
    return blocks_syscall(SYS_FILE_STAT, (long)path, (long)out, 0);
}

static inline long blocks_fstat(long fd, void *out) {
    return blocks_syscall(SYS_FILE_FSTAT, fd, (long)out, 0);
}

static inline long blocks_open_directory(const char *path) {
    return blocks_syscall(SYS_DIR_OPEN, (long)path, 0, 0);
}

static inline long blocks_read_directory(long dirfd, void *out) {
    return blocks_syscall(SYS_DIR_READ, dirfd, (long)out, 0);
}

static inline long blocks_mmap(long len, long prot, long flags) {
    return blocks_syscall(SYS_MEMORY_MAP, len, prot, flags);
}

static inline long blocks_munmap(long addr, long len) {
    return blocks_syscall(SYS_MEMORY_UNMAP, addr, len, 0);
}

static inline long blocks_signal(long sig, long handler) {
    return blocks_syscall(SYS_PROCESS_SIGNAL, sig, handler, 0);
}

static inline long blocks_sleep(long ms) {
    return blocks_syscall(SYS_TIME_SLEEP, ms, 0, 0);
}

static inline long blocks_clock_ms(void) {
    return blocks_syscall(SYS_CLOCK_MS, 0, 0, 0);
}

static inline long blocks_socket(long domain, long type, long protocol) {
    return blocks_syscall(SYS_NET_SOCKET, domain, type, protocol);
}

static inline long blocks_connect(long fd, long ip, long port) {
    return blocks_syscall(SYS_NET_CONNECT, fd, ip, port);
}

static inline long blocks_bind(long fd, long ip, long port) {
    return blocks_syscall(SYS_NET_BIND, fd, ip, port);
}

static inline long blocks_listen(long fd, long backlog) {
    return blocks_syscall(SYS_NET_LISTEN, fd, backlog, 0);
}

static inline long blocks_accept(long fd) {
    return blocks_syscall(SYS_NET_ACCEPT, fd, 0, 0);
}

static inline long blocks_recv_nb(int fd, void *buf, long len) {
    return blocks_syscall(SYS_NET_RECV_NB, fd, (long)buf, len);
}

#endif /* !__ASSEMBLER__ */

#endif /* BLOCKS_SYSCALL_H */
