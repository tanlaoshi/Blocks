/*
 * Save.c — 写 BLOCKS.DB 与 THEME.CFG（编排；PR-F-theme-1）
 * 分辨率 mode= 只在 DB；CFG 管配色等其它键。
 * CFG：SaveConfig.c；DB：SaveDb.c；填值：SaveFormat.c
 */
#include "Theme.h"
#include "ThemePrivate.h"
#include "HalConsole.h"

int ThemeSave(void) {
    char Buf[320];
    UINTN N;
    THEME_SAVE_VALS Vals;
    int CfgSt;
    int DbSt;
    static int sBusy;

    if (sBusy) {
        HalConsoleWriteSerial("Theme: save reenter skipped\n");
        return -1;
    }
    sBusy = 1;

    ThemeSaveFillVals(&Vals);
    /*
     * 先写 DB：Boot / QEMU edid 认 mode=。CFG 可跳过未变内容，
     * 但分辨率只在 DB，故 DB 每次都写。
     */
    DbSt = ThemeSaveWriteDb(&Vals);
    N = ThemeSaveBuildCfg(Buf, sizeof(Buf), &Vals);
    CfgSt = ThemeSaveWriteCfg(Buf, N);
    sBusy = 0;
    if (DbSt < 0 && CfgSt < 0) {
        return -1;
    }
    /*
     * 故意不在 Write 后立刻 Read 同文件：QEMU fat:rw/vvfat 会断言
     * get_cluster_count_for_direntry。Guest 以内存 + DB 为准。
     */
    return 0;
}
