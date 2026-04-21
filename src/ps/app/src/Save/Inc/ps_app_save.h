#ifndef PS_APP_SAVE_H
#define PS_APP_SAVE_H

#include "xstatus.h"

#include "Save/Inc/ps_app_save_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 清空 save 运行态并重置 DDR 保存窗口（按窗口布局而非文件布局）。 */
void PsAppSave_Reset(PsAppSaveContext *ctx);
/* ROM 装载阶段调用：
 * 创建保存目录并尝试从 SD 把存档“加载+展开”到 core 可读窗口。 */
XStatus PsAppSave_PrepareForRom(PsAppSaveContext *ctx, const char *rom_path);
/* 周期服务：
 * 轮询 SAVE_STATUS 计数器，按 quiet/hard 双阈值触发自动落盘。 */
void PsAppSave_Service(PsAppSaveContext *ctx);
/* ROM 卸载前兜底：
 * 若仍有 dirty 数据，强制立即落盘。 */
XStatus PsAppSave_FlushIfDirty(PsAppSaveContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
