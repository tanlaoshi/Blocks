#include <Blocks/thread.h>
#include <Blocks/Process.h>
/*
 * 任务管理器。文字跟系统语言：读 BLOCKS.DB 的 lang=（缺省汉语，与内核一致）。
 * 列表里用上下键或「下一项」移动选择。刷新 / 下一项 / 结束三个按钮保留。
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <Ui.h>
#include <Blocks/task.h>

static TASK_SNAP gSnap;
static int gIndex;
static int gWid = -1;
static int gZh = 1;

static const char *Tr(const char *En, const char *Zh) {
    return gZh ? Zh : En;
}

static void LoadLang(void) {
    FILE *F;
    char Buf[512];
    size_t N;
    char *P;

    gZh = 1;
    F = fopen("BLOCKS.DB", "r");
    if (!F) {
        return;
    }
    N = fread(Buf, 1, sizeof(Buf) - 1, F);
    fclose(F);
    Buf[N] = 0;
    P = Buf;
    while (*P) {
        if (P[0] == 'l' && P[1] == 'a' && P[2] == 'n' && P[3] == 'g' && P[4] == '=') {
            gZh = (P[5] == 'z' && P[6] == 'h');
            return;
        }
        while (*P && *P != '\n') {
            P++;
        }
        if (*P == '\n') {
            P++;
        }
    }
}

static const char *StateWord(uint32_t Flags) {
    if (Flags & TASK_F_ZOMBIE) {
        return Tr("zombie", "僵尸");
    }
    if (Flags & TASK_F_BLOCKED) {
        return Tr("blocked", "阻塞");
    }
    if (Flags & TASK_F_CURRENT) {
        return Tr("running", "运行");
    }
    return Tr("ready", "就绪");
}

static const char *KindWord(uint32_t Flags) {
    if (Flags & TASK_F_USER) {
        return Tr("User", "用户");
    }
    return Tr("Kernel", "内核");
}

static void Paint(void) {
    char Page[1024];
    int Used;
    unsigned i;

    if (gWid < 0) {
        return;
    }
    if (gSnap.Count == 0) {
        snprintf(Page, sizeof(Page), "%s", Tr("no tasks", "没有任务"));
        UiSetLabel(gWid, Page);
        return;
    }
    if (gIndex < 0 || (unsigned)gIndex >= gSnap.Count) {
        gIndex = 0;
    }
    Used = snprintf(Page, sizeof(Page), "\t%s\t%s\t%s\t%s\n",
                    Tr("pid", "编号"), Tr("name", "名称"),
                    Tr("kind", "类型"), Tr("state", "状态"));
    if (Used < 0 || Used >= (int)sizeof(Page)) {
        Used = 0;
    }
    for (i = 0; i < gSnap.Count && Used < (int)sizeof(Page) - 1; i++) {
        const TASK_ENTRY *E = &gSnap.Tasks[i];
        int N;

        N = snprintf(Page + Used, sizeof(Page) - (size_t)Used,
                     "%c\t%d\t%s\t%s\t%s\n",
                     (int)i == gIndex ? '>' : ' ', (int)E->Pid, E->Name,
                     KindWord(E->Flags), StateWord(E->Flags));
        if (N < 0 || Used + N >= (int)sizeof(Page)) {
            break;
        }
        Used += N;
    }
    UiSetLabel(gWid, Page);
}

static int Refresh(void) {
    if (blocks_task_snap(&gSnap) != 0) {
        return -1;
    }
    if (gSnap.Magic != TASK_SNAP_MAGIC || gSnap.Version != TASK_SNAP_VER) {
        return -1;
    }
    if (gSnap.Count == 0) {
        gIndex = 0;
    } else if (gIndex < 0 || (unsigned)gIndex >= gSnap.Count) {
        gIndex = (int)gSnap.Count - 1;
    }
    Paint();
    return 0;
}

static void Move(int Delta) {
    if (gSnap.Count == 0) {
        return;
    }
    gIndex += Delta;
    while (gIndex < 0) {
        gIndex += (int)gSnap.Count;
    }
    while ((unsigned)gIndex >= gSnap.Count) {
        gIndex -= (int)gSnap.Count;
    }
    Paint();
}

static void KillCurrent(void) {
    const TASK_ENTRY *E;

    if (gSnap.Count == 0 || gIndex < 0 || (unsigned)gIndex >= gSnap.Count) {
        UiSetLabel(gWid, Tr("nothing to end", "没有可结束的任务"));
        return;
    }
    E = &gSnap.Tasks[gIndex];
    if (!(E->Flags & TASK_F_USER)) {
        UiSetLabel(gWid, Tr("refuse: kernel task", "不能结束内核任务"));
        return;
    }
    if (E->Pid == gSnap.SelfPid) {
        UiSetLabel(gWid, Tr("refuse: self", "不能结束自己"));
        return;
    }
    if (ProcessKill((pid_t)E->Pid, SIGKILL) != 0) {
        UiSetLabel(gWid, Tr("end failed", "结束失败"));
        return;
    }
    Refresh();
}

static void DrainClicks(void) {
    int Ev;

    for (;;) {
        Ev = UiPoll(gWid);
        if (Ev <= 0 || Ev == UI_EVENT_CLOSE) {
            break;
        }
    }
}

int main(void) {
    int Ev;

    LoadLang();
    gWid = UiCreateWindow(Tr("Task Manager", "任务管理器"), 720, 600);
    if (gWid < 0) {
        printf("taskmgr: create fail\n");
        return 1;
    }
    if (UiAddButton(gWid, 0, Tr("Refresh", "刷新")) != 0 ||
        UiAddButton(gWid, 1, Tr("Next", "下一项")) != 0 ||
        UiAddButton(gWid, 2, Tr("Kill", "结束")) != 0) {
        printf("taskmgr: button fail\n");
        return 1;
    }
    DrainClicks();
    gIndex = 0;
    if (Refresh() != 0) {
        UiSetLabel(gWid, Tr("snapshot failed", "读取任务失败"));
    }

    for (;;) {
        Ev = UiPoll(gWid);
        if (Ev == UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (Ev == UI_BUTTON_EVENT(0)) {
            if (Refresh() != 0) {
                UiSetLabel(gWid, Tr("refresh failed", "刷新失败"));
            }
        } else if (Ev == UI_BUTTON_EVENT(1)) {
            Move(1);
        } else if (Ev == UI_BUTTON_EVENT(2)) {
            KillCurrent();
        } else if (UI_IS_KEY(Ev)) {
            int Hid = UI_KEY_CODE(Ev);

            if (Hid == 0x52) {
                Move(-1);
            } else if (Hid == 0x51) {
                Move(1);
            }
        } else {
            ThreadYield();
        }
    }
    return 0;
}
