/*
 * PrivateDeclarations.h — Ksend 内部函数声明（Private.h 拆出，≤300）
 */
#ifndef KSEND_PRIVATE_DECLARATIONS_H
#define KSEND_PRIVATE_DECLARATIONS_H

/* —— Ksend.c —— */
void KsendNote(const char *Text);
int KsendInfo(void);
int KsendSelfTest(void);
int KsendTransfer(const char *TargetPath, int Reboot, int Backup);
int KsendTransferSized(const char *TargetPath, int Reboot, int Backup,
                       UINT32 ExpectedSize);

/* —— Serial.c / Transport.c —— */
void KsendSerialWrite(const char *Text);
void KsendSerialPutByte(UINT8 Value);
int KsendSerialGetByte(UINT8 *Out, UINT32 TimeoutMilliseconds);
/* 吃掉接收方向上没人认领的字节：静默 QuietMilliseconds 或总时长 LimitMilliseconds 到就停 */
void KsendSerialDrainInput(UINT32 QuietMilliseconds, UINT32 LimitMilliseconds);
/* 等移位寄存器发空（TEMT）/TCP 推入栈；复位前必须调用，否则 KSEND OK 可能被截断 */
void KsendSerialDrainTransmitter(void);
void KsendSerialWaitPeerAcknowledge(void);
/* 25 MHz rdtime 换算的毫秒计数 / 忙等（复位时序用） */
UINT32 KsendMilliseconds(void);
void KsendDelayMilliseconds(UINT32 Milliseconds);
void KsendTransportUseSerial(void);
void KsendTransportUseSocket(int Socket);
int KsendTransportSocket(void);
void KsendTransportPutByte(UINT8 Value);
int KsendTransportGetByte(UINT8 *Out, UINT32 TimeoutMilliseconds);
void KsendTransportDrainInput(UINT32 QuietMilliseconds, UINT32 LimitMilliseconds);
/* 应用缓冲 → lwIP（中间批量用；不等对端 ACK） */
void KsendTransportDrainTransmitter(void);
/* 上式之后再等 snd_queuelen==0（READY/整表末尾必须；中间 512B 勿用） */
void KsendTransportWaitPeerAcknowledge(void);
/* 1=发送正常；0=曾排空超时（Put/Drain 已标 broken，勿再死等） */
int KsendTransportSendHealthy(void);
/* 1=串口收到 Ctrl-C（含 TCP serve 期间） */
int KsendTransportUserAborted(void);
/* —— Serve.c / ServeClient.c / ServePreferences.c —— */
int KsendServe(UINT16 Port, UINT32 WaitMilliseconds);
UINT16 KsendServeDefaultPort(void);
int KsendServeListenStart(UINT16 Port, UINT32 WaitMilliseconds);
void KsendServeListenStop(void);
void KsendServePoll(void);
KSEND_SERVE_STATE KsendServeState(void);
int KsendServeLastResult(void);
UINT16 KsendServeListenPort(void);
void KsendServeRequestAbort(void);
int KsendServeRunClient(int ClientSocket);
UINT16 KsendServePreferencePort(void);
void KsendServePreferenceSetPort(UINT16 Port);
int KsendServePreferenceReboot(void);
void KsendServePreferenceSetReboot(int On);
int KsendServePreferenceBackup(void);
void KsendServePreferenceSetBackup(int On);

/* —— Text.c —— */
void KsendTextReset(KSEND_TEXT *Text, char *Buffer, int Capacity);
void KsendTextPut(KSEND_TEXT *Text, const char *String);
void KsendTextPutUnsigned(KSEND_TEXT *Text, UINT32 Value);
void KsendTextPutHex(KSEND_TEXT *Text, UINT32 Value);
void KsendTextPutField(KSEND_TEXT *Text, const char *Key, UINT32 Value);
void KsendTextPutHexField(KSEND_TEXT *Text, const char *Key, UINT32 Value);
void KsendTextPutSignedField(KSEND_TEXT *Text, const char *Key, int Value);
void KsendTextFlush(KSEND_TEXT *Text);

/* —— CyclicRedundancyCheck.c —— */
UINT32 KsendCrc32Update(UINT32 Crc, const UINT8 *Data, UINT32 Length);
UINT32 KsendCrc32Final(UINT32 Crc);

/* —— Report.c（协议行 / 人话行） —— */
void KsendPrintImage(const char *Tag, const KSEND_IMAGE *Image);
void KsendPrintReason(const char *Tag, const char *Reason, int Error);
void KsendReadyLine(const KSEND_IMAGE *Image);
UINT32 KsendReadyWindowBlocks(void);
void KsendBlockTable(const KSEND_IMAGE *Image);
void KsendPhaseNote(const char *Name, UINT32 Milliseconds);
void KsendPhaseNoteReceive(const KSEND_RUN *Run);
void KsendPhaseNoteFinish(const KSEND_RUN *Run);
void KsendSuccessLine(const KSEND_RUN *Run);
void KsendFailLine(int Failure, UINT32 Block, UINT32 Want, UINT32 Got, int Error);
void KsendAbortLine(UINT32 Written);
void KsendRestoredLine(const KSEND_IMAGE *Image);

/* —— Receive.c —— */
/* 收块直到收尾帧：0=收到收尾（Run->NewSize/TrailerCrc 已就绪）；-1=失败/放弃 */
int KsendReceiveBlocks(KSEND_RUN *Run);
void KsendReceiveBatchReset(void);
int KsendReceiveBatchFlush(KSEND_RUN *Run);
/*
 * Accept：1=本块已入批（可能刚 flush）；2=须先发 Ack 表再以同块重入；0=失败。
 * OutAckCount = 最近一次 flush 的块数。
 */
int KsendReceiveBatchAccept(KSEND_RUN *Run, UINT32 Index, UINT32 Length,
                            const UINT8 *Data, UINT32 BlockCrc,
                            UINT32 *OutAckCount);
UINT32 KsendReceiveBatchAckCount(void);
int KsendReceiveBatchAckAt(UINT32 Slot, UINT32 *OutIndex, UINT32 *OutLength,
                           UINT32 *OutCrc);
UINT32 KsendReceiveBatchPendingBlocks(void);

/* 1=接收方向仍有未读字节（TCP 缓冲或应用缓冲） */
int KsendTransportInputPending(void);

/* —— File.c —— */
/* 旧文件（备份）图：restore 的判据 */
extern KSEND_IMAGE gKsendOldImage;
/* 传输中「期望的新内容」图；info 也拿它当只打印的草稿 */
extern KSEND_IMAGE gKsendNewImage;

/* KSEND_IO_CHUNK_BYTES 复用缓冲（自检/接收落盘共用） */
UINT8 *KsendFileBuffer(void);
/* 读满 Want 才认；短读 → FAT_ERR_IO（KsendFileVerify 与扫描共用） */
int KsendFileReadExact(const char *Path, UINT32 Offset, UINT32 Want,
                       UINT32 *OutGot);
UINT32 KsendFileBlockCrcOf(const UINT8 *Data, UINT32 Length);
int KsendFileDelete(const char *Path);
int KsendFileSize(const char *Path, UINT32 *OutSize);
/* 写在任意偏移（FAT 层按需扩簇；0 长度由调用方避免） */
int KsendFileWriteAt(const char *Path, UINT32 Offset, const UINT8 *Data,
                     UINT32 Length);
/* 顺序读整文件，填 Size/BlockCount/Crc/BlockCrc */
int KsendFileScan(const char *Path, UINT32 Size, KSEND_IMAGE *Image);
/* 复制并顺带算出被复制内容的图（源先读、目标先删） */
int KsendFileCopy(const char *Source, const char *Target, UINT32 Size,
                  KSEND_IMAGE *Image);
/* 写一块（Path 的 Index*4096 起，Length 1..4096）；接收路径改为批量写 */
int KsendFileWriteBlock(const char *Path, UINT32 Index, const UINT8 *Data,
                        UINT32 Length);
/* 把 [Offset, Offset+Length) 写成 0（FAT 无 truncate 时的兜底） */
int KsendFileZeroRange(const char *Path, UINT32 Offset, UINT32 Length);
/* —— FileVerify.c —— */
/* 1=一致；0=块不符（OutBlock/Want/Got）；-1=整体 CRC 不符 */
int KsendFileVerify(const char *Path, UINT32 Size, const KSEND_IMAGE *Expected,
                    UINT32 ExpectedCrc, UINT32 *OutBlock, UINT32 *OutWant,
                    UINT32 *OutGot);
/* 删目标再从备份重写并校验（尺寸与内容都回到备份） */
int KsendFileRestore(const char *BackupPath, const char *TargetPath,
                     const KSEND_IMAGE *Expected);
int KsendFileSelfTest(KSEND_SELFTEST *Out);

/* —— Cache.c —— */
/* 校验并加载 <目标>.crc；命中才返回 FAT_OK（Image 完整可用），否则调用方
 * 必须退回逐块读文件的慢路径。缓存不是事实来源，只是加速。 */
int KsendCacheLoad(const char *TargetPath, UINT32 TargetSize,
                   KSEND_IMAGE *Image);
/* 让 <目标>.crc 失效；失败/回滚/缩小时用（删掉比猜测旧表是否还一致安全） */
int KsendCacheDrop(const char *TargetPath);
/* 整文件读回校验通过后写 <目标>.crc；失败只让下次变慢，不算本次失败 */
int KsendCacheSave(const char *TargetPath, const KSEND_IMAGE *Image);

/* —— Reset.c —— */
/* 复位：HalCpuReboot；成功则不返回 */
void KsendReboot(void);

/* —— TransferPaths.c —— */
void KsendRunReset(KSEND_RUN *Run);
int KsendPathsPrepare(const char *Requested, KSEND_RUN *Run);
void KsendVolumeRelease(KSEND_RUN *Run);
void KsendPathsNote(const KSEND_RUN *Run);
/* 1=本次写入走 Target.new（须 READY 空表全量收） */
int KsendRunUsesStaging(const KSEND_RUN *Run);
/* OldSize>0：删并指向 Target.new；否则 WritePath=Target */
int KsendStagingPrepare(KSEND_RUN *Run);
/* 校验通过后：删 Target，把 WritePath 改名为 Target */
int KsendStagingInstall(KSEND_RUN *Run);
/* 失败时丢掉未安装的 .new（Target 未动则不必 restore） */
void KsendStagingDiscard(const KSEND_RUN *Run);
void KsendImageCopy(KSEND_IMAGE *Destination, const KSEND_IMAGE *Source);
/* 目标不存在 / OldSize=0：须清空，勿沿用上次 READY 表（Duo 曾假 size=98304） */
void KsendImageClear(KSEND_IMAGE *Image);

/* —— TransferFinish.c —— */
int KsendFinishImage(KSEND_RUN *Run);
void KsendRollBack(const KSEND_RUN *Run);
void KsendCacheDropAfterFailure(const KSEND_RUN *Run);

#endif /* KSEND_PRIVATE_DECLARATIONS_H */
