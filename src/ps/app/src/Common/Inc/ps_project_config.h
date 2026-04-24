#ifndef PS_PROJECT_CONFIG_H
#define PS_PROJECT_CONFIG_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS_APP_FB_REGION_BASE_ADDR      0x18000000U
#define PS_APP_FB_FRAME_STORE_BYTES     0x00200000U
#define PS_APP_HDMI_WIDTH               640U
#define PS_APP_HDMI_HEIGHT              480U
#define PS_APP_HDMI_BPP                 4U
/* GBA save 窗口位于 DDR 起始段：
 * 0x1000_0000 ~ 0x100B_FFFF (共 768KiB) 由 save/WRAM/映射窗口共用。
 * 其中 save 逻辑数据并非“线性字节阵列”，而是 core 侧按 32-bit 槽位展开。 */
#define PS_APP_GBA_SAVE_REGION_BASE_ADDR 0x10000000U
/* gba_mister 的外部保存窗口按 32-bit 粒度映射，SRAM/FLASH/EEPROM 逻辑字节在
 * DDR 中按 stride=4 展开存储。为覆盖 FLASH1M（128KiB 逻辑 -> 512KiB 窗口），
 * 这里必须保留 512KiB。 */
#define PS_APP_GBA_SAVE_REGION_MAX_BYTES (512U * 1024U)
#define PS_APP_GBA_SAVE_SRAM_BYTES      (64U * 1024U)
#define PS_APP_GBA_SAVE_FLASH_BYTES     (128U * 1024U)
#define PS_APP_GBA_SAVE_EEPROM_BYTES    (8U * 1024U)
/* 关键约束：
 * 磁盘文件尺寸使用“逻辑字节数”（64KiB/128KiB/8KiB），
 * DDR 访问窗口使用“展开字节数”（逻辑 * 4）。
 * 任何存档读写都必须先做 expand/compress 变换，否则会出现“文件存在但游戏读不到存档”。 */
#define PS_APP_GBA_SAVE_EXPAND_STRIDE   4U
#define PS_APP_GBA_SAVE_SRAM_WINDOW_BYTES   (PS_APP_GBA_SAVE_SRAM_BYTES * PS_APP_GBA_SAVE_EXPAND_STRIDE)
#define PS_APP_GBA_SAVE_FLASH_WINDOW_BYTES  (PS_APP_GBA_SAVE_FLASH_BYTES * PS_APP_GBA_SAVE_EXPAND_STRIDE)
#define PS_APP_GBA_SAVE_EEPROM_WINDOW_BYTES (PS_APP_GBA_SAVE_EEPROM_BYTES * PS_APP_GBA_SAVE_EXPAND_STRIDE)
/* ROM 起始地址后移到 0x100C0000，显式避开前面的 save/softmap 窗口。 */
#define PS_APP_GBA_ROM_REGION_BASE_ADDR 0x100C0000U
#define PS_APP_GBA_ROM_REGION_MAX_BYTES (32U * 1024U * 1024U)
#define PS_APP_GBA_SAVESTATE_REGION_BASE_ADDR 0x14000000U
#define PS_APP_GBA_SAVESTATE_SLOT_BYTES (512U * 1024U)
/* FatFs is configured for UTF-8 long file names. A 255-character Chinese LFN
 * can occupy up to 765 bytes before the directory prefix and terminator. */
#define PS_APP_ROM_PATH_MAX_CHARS       896U
#define PS_APP_DEFAULT_ROM_SD_PATH      "0:/games/bjg.gba"
#define PS_APP_GAMES_SD_DIR             "0:/games"
#define PS_APP_AUTOLOAD_DEFAULT_ROM     0U
#define PS_APP_SAVE_SD_DIR              "0:/saves"
#define PS_APP_SAVESTATE_SD_DIR         "0:/savestates"
#define PS_APP_CHEAT_SD_DIR             "0:/cheats"
#define PS_APP_RTC_SD_DIR               "0:/rtc"
#define PS_APP_BIOS_SD_PATH             "0:/boot.rom"
#define PS_APP_GBA_BIOS_BYTES           (16U * 1024U)
#define PS_APP_GBA_BIOS_WORDS           (PS_APP_GBA_BIOS_BYTES / 4U)
#define PS_APP_GBA_BIOS_WRITE_TIMEOUT_LOOPS 2000000U

#define PS_APP_SYS_TASK_STACK_WORDS       (configMINIMAL_STACK_SIZE * 10U)
#define PS_APP_SYS_TASK_PRIORITY          (tskIDLE_PRIORITY + 3U)
#define PS_APP_VIDEO_TASK_STACK_WORDS     (configMINIMAL_STACK_SIZE * 8U)
#define PS_APP_VIDEO_TASK_PRIORITY        (tskIDLE_PRIORITY + 2U)
/* Console path includes command parsing and large help output. */
#define PS_APP_CONSOLE_TASK_STACK_WORDS   (configMINIMAL_STACK_SIZE * 10U)
#define PS_APP_CONSOLE_TASK_PRIORITY      (tskIDLE_PRIORITY + 2U)
#define PS_APP_UI_TASK_STACK_WORDS        (configMINIMAL_STACK_SIZE * 14U)
#define PS_APP_UI_TASK_PRIORITY           (tskIDLE_PRIORITY + 2U)
/* 关键防坑：
 * save 线程会经过 FatFs + SD 驱动 + 日志打印的深调用链。
 * 之前栈偏小时出现过上下文指针异常（例如 0x00736576）并导致保存后卡死，
 * 因此这里保留更大的栈余量，优先保证稳定性。 */
#define PS_APP_SAVE_TASK_STACK_WORDS      (configMINIMAL_STACK_SIZE * 32U)
#define PS_APP_SAVE_TASK_PRIORITY         (tskIDLE_PRIORITY + 1U)
#define PS_APP_UART_BAUDRATE              115200U
#define PS_APP_BTN4_MIO_PIN               50U
#define PS_APP_BTN5_MIO_PIN               51U
#define PS_APP_BTN4_MASK                  (1U << 0U)
#define PS_APP_BTN5_MASK                  (1U << 1U)
#define PS_APP_IRQ_MASK_VSYNC             0x1U
#define PS_APP_IRQ_MASK_ERROR             0x2U
#define PS_APP_IRQ_MASK_DEFAULT           (PS_APP_IRQ_MASK_VSYNC | PS_APP_IRQ_MASK_ERROR)
#define PS_APP_MONITOR_INTERVAL_MS        10U
#define PS_APP_VIDEO_PRESENT_INTERVAL_MS  1U
/* USB/XInput 报告通常在 1ms 量级到达；按键提交若仍跟 10ms 慢轮询绑定，
 * 短按会随机表现成漏按或多按，因此单独给输入路径更高的服务频率。 */
#define PS_APP_INPUT_SERVICE_INTERVAL_MS  1U
#define PS_APP_INPUT_ENABLE_DEFAULT       1U
#define PS_APP_UI_SERVICE_INTERVAL_MS     10U
#define PS_APP_UI_MAX_GAMES               128U
#define PS_APP_UI_GAME_NAME_MAX_CHARS     768U
#define PS_APP_UI_LT_EXIT_THRESHOLD       250U
#define PS_APP_UI_LT_EXIT_HOLD_MS         5000U
/* 板载系统键退出：BTN4+BTN5 组合长按 2 秒触发一次退出。
 * 这里保留和音量按键并存，允许先发生一次音量变化再退出。 */
#define PS_APP_UI_BOARD_EXIT_HOLD_MS      2000U
#define PS_APP_UI_BOARD_EXIT_BTN_MASK     (PS_APP_BTN4_MASK | PS_APP_BTN5_MASK)
#define PS_APP_INPUT_MAP_AB_BY_POSITION   0U
#define PS_APP_INPUT_LSTICK_DEADZONE      12000
#define PS_APP_INPUT_TRIGGER_THRESHOLD    80U
#define PS_APP_INPUT_STABLE_SAMPLE_COUNT  2U
/* 非摇杆按键采用“单次短按脉冲”模式：8ms 通常既能被游戏采样到，又不容易跨多帧。 */
#define PS_APP_INPUT_SHORT_PULSE_TICKS      8U
/* 行业常见做法：按下和抬起分别去抖，抬起阈值稍长以抑制松手抖动导致的重复触发。 */
#define PS_APP_INPUT_PRESS_DEBOUNCE_TICKS   2U
#define PS_APP_INPUT_RELEASE_DEBOUNCE_TICKS 6U
#define PS_APP_USBHOST_ENABLE_DEFAULT     1U
/* USB 热插拔识别采用“物理层去抖 + 枚举观察窗口 + 断开兜底”：
 * 先确认端口电平稳定，再触发枚举/恢复，避免抖动导致误判。 */
#define PS_APP_USBHOST_CONNECT_DEBOUNCE_TICKS        30U
#define PS_APP_USBHOST_DISCONNECT_DEBOUNCE_TICKS     30U
#define PS_APP_USBHOST_ENUM_OBSERVE_TICKS            250U
#define PS_APP_USBHOST_SCAN_RETRY_TICKS              40U
#define PS_APP_USBHOST_STALE_DETACH_TICKS            80U
#define PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_BASE_TICKS 400U
#define PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_MAX_TICKS  3000U
/* 保存落盘策略：
 * quiet_ticks 控制“最后一次写入事件之后等待多久再落盘”，
 * hard_ticks 是兜底上限（即使一直有零碎写入，也必须强制落盘一次）。 */
#define PS_APP_SAVE_FLUSH_QUIET_TICKS     10U
#define PS_APP_SAVE_FLUSH_HARD_TICKS      200U
#define PS_APP_REWIND_CKPT_INTERVAL_MS    10000U
#define PS_APP_REWIND_CKPT_COUNT          120U
#define PS_APP_TRACE_DEFAULT_SAMPLES      20U
#define PS_APP_TRACE_DEFAULT_INTERVAL_MS  100U
#define PS_APP_TRACE_MAX_SAMPLES          200U
#define PS_APP_FRAME_HASH_SAMPLES         64U
#define PS_APP_AUTO_BOOT_AUDIT_DELAY_MS   250U
#define PS_APP_AUTO_STALL_AUDIT_SAMPLES   50U
#define PS_APP_MEM_STATE_WAIT_SDRAM       0x0000000BU

#define PS_APP_FBSCAN_INTERVAL_TICKS      100U
#define PS_APP_FBSCAN_SAMPLE_LINES        16U
#define PS_APP_FBSCAN_SAMPLE_PIXELS       8U
#define PS_APP_FBSCAN_MAX_DUMPS           5U
#define PS_APP_FBSCAN_WHITE_PIXEL         0x00FFFFFFU
#define PS_APP_FBSCAN_BLACK_PIXEL         0x00000000U

/* 启动阶段需要保持的控制位：
 * bit1=LOCK_SPEED, bit4=SRAM_FLASH_EN。
 * 这里使用局部常量，避免 Common 配置头对 Gba 寄存器头产生传递依赖。 */
#define PS_APP_GBA_CTRL_BOOT_REQUIRED  ((1U << 1U) | (1U << 4U))

#define PS_APP_SAVE_STATUS_SRAM_DIRTY      (1U << 0)
#define PS_APP_SAVE_STATUS_FLASH_DIRTY     (1U << 1)
#define PS_APP_SAVE_STATUS_EEPROM_DIRTY    (1U << 2)
#define PS_APP_SAVE_STATUS_DIRTY_MASK      (PS_APP_SAVE_STATUS_SRAM_DIRTY | \
                                           PS_APP_SAVE_STATUS_FLASH_DIRTY | \
                                           PS_APP_SAVE_STATUS_EEPROM_DIRTY)

#define PS_APP_GBA_FRAME_WIDTH            240U
#define PS_APP_GBA_FRAME_HEIGHT           160U
#define PS_APP_GBA_FB_CAPTURE_WORDS       ((PS_APP_GBA_FRAME_WIDTH * PS_APP_GBA_FRAME_HEIGHT) / 2U)
#define PS_APP_GBA_FB_CAPTURE_ROW_WORDS   (PS_APP_GBA_FB_CAPTURE_WORDS / PS_APP_GBA_FRAME_HEIGHT)
#define PS_APP_GBA_FB_CAPTURE_FRAME_BYTES (PS_APP_GBA_FB_CAPTURE_WORDS * sizeof(u32))

#ifdef __cplusplus
}
#endif

#endif
