#ifndef PS_PROJECT_CONFIG_H
#define PS_PROJECT_CONFIG_H

#include "FreeRTOS.h"
#include "task.h"

#include "Gba/Inc/ps_gba_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS_APP_FB_REGION_BASE_ADDR      0x18000000U
#define PS_APP_FB_FRAME_STORE_BYTES     0x00200000U
#define PS_APP_HDMI_WIDTH               640U
#define PS_APP_HDMI_HEIGHT              480U
#define PS_APP_HDMI_BPP                 4U
#define PS_APP_GBA_SAVE_REGION_BASE_ADDR 0x10000000U
#define PS_APP_GBA_SAVE_REGION_MAX_BYTES (256U * 1024U)
#define PS_APP_GBA_SAVE_SRAM_BYTES      (64U * 1024U)
#define PS_APP_GBA_SAVE_FLASH_BYTES     (128U * 1024U)
#define PS_APP_GBA_SAVE_EEPROM_BYTES    (8U * 1024U)
#define PS_APP_GBA_ROM_REGION_BASE_ADDR 0x100C0000U
#define PS_APP_GBA_ROM_REGION_MAX_BYTES (32U * 1024U * 1024U)
#define PS_APP_ROM_PATH_MAX_CHARS       128U
#define PS_APP_DEFAULT_ROM_SD_PATH      "0:/games/bjg.gba"
#define PS_APP_SAVE_SD_DIR              "0:/saves"

#define PS_APP_SYS_TASK_STACK_WORDS       (configMINIMAL_STACK_SIZE * 10U)
#define PS_APP_SYS_TASK_PRIORITY          (tskIDLE_PRIORITY + 3U)
#define PS_APP_CONSOLE_TASK_STACK_WORDS   (configMINIMAL_STACK_SIZE * 8U)
#define PS_APP_CONSOLE_TASK_PRIORITY      (tskIDLE_PRIORITY + 2U)
#define PS_APP_UART_BAUDRATE              115200U
#define PS_APP_BTN4_MIO_PIN               50U
#define PS_APP_BTN5_MIO_PIN               51U
#define PS_APP_BTN4_MASK                  (1U << 0U)
#define PS_APP_BTN5_MASK                  (1U << 1U)
#define PS_APP_IRQ_MASK_VSYNC             0x1U
#define PS_APP_IRQ_MASK_ERROR             0x2U
#define PS_APP_IRQ_MASK_DEFAULT           (PS_APP_IRQ_MASK_VSYNC | PS_APP_IRQ_MASK_ERROR)
#define PS_APP_MONITOR_INTERVAL_MS        10U
/* USB/XInput 报告通常在 1ms 量级到达；按键提交若仍跟 10ms 慢轮询绑定，
 * 短按会随机表现成漏按或多按，因此单独给输入路径更高的服务频率。 */
#define PS_APP_INPUT_SERVICE_INTERVAL_MS  1U
#define PS_APP_INPUT_ENABLE_DEFAULT       1U
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
#define PS_APP_SAVE_FLUSH_QUIET_TICKS     25U
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

#define PS_APP_GBA_CTRL_BOOT_REQUIRED \
    (GBA_CTRL_LOCK_SPEED | GBA_CTRL_SRAM_FLASH_EN)

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
