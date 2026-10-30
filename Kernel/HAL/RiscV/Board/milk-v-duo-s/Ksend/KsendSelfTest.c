/*
 * KsendSelfTest.c — `ksend selftest`：转调公共文件系统写自检
 *
 * 判据、图案、CRC 全在 Services/FileSystem/SelfTest.c，与 `fs selftest` 同一套
 * （8 KB / 盐 0x5A / CRC 87fb2824）。这里只搬运字段，让 Ksend.c 继续打印原来的
 * `KSEND SELFTEST OK size=8192 crc=87fb2824`——那是 Duo 真机判据，一字不改。
 *
 * 为什么不调打印版 FileSystemWriteSelfTest()：它会多打
 * `fs selftest: …` 与 `FS SELFTEST OK …` 两行，改变 ksend 的串口输出。
 *
 * 差异（失败行才可见）：公共自检把步骤拆成 1..5（创建/写/读回/覆写/删除），
 * 旧实现在这里只有 step=1（首遍）和 step=2（覆写）；OK 行完全相同。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

int KsendFileSelfTest(KSEND_SELFTEST *Out) {
    FS_WRITE_SELFTEST Result;
    int Err = FileSystemWriteSelfTestRun(&Result);

    Out->Size = Result.Size;
    Out->Crc = Result.Crc;
    Out->Step = Result.Step;
    Out->FailOffset = (Result.FailOffset == FS_WRITE_SELFTEST_NO_OFFSET)
                          ? KSEND_FAIL_OFFSET_NONE
                          : Result.FailOffset;
    Out->FailWant = Result.FailWant;
    Out->FailGot = Result.FailGot;
    return Err;
}
