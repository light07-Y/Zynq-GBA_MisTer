#include "FreeRTOS.h"
#include "task.h"

#include "sleep.h"
#include "xil_cache.h"
#include "xil_printf.h"

#include "Common/Inc/ps_project_config.h"
#include "App/Inc/ps_app_console.h"
#include "Common/Inc/ps_app_context.h"
#include "Diagnostics/Inc/ps_app_diag.h"
#include "App/Inc/ps_app_runtime.h"
#include "Ui/Inc/ps_app_ui.h"
#include "Video/Inc/ps_app_video.h"

static PsAppContext s_ps_app_context;

int main(void) {
    BaseType_t ok;
    XStatus status;

    Xil_DCacheEnable();
    Xil_ICacheEnable();

    PsAppContext_Init(&s_ps_app_context);
    status = PsAppRuntime_InitUart(&s_ps_app_context.runtime_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[PS] uart init failed: %d\r\n", status);
        return 1;
    }

    xil_printf("\r\n=== Zynq GBA PS Runtime(Made by SDJU Panziyu) ===\r\n");

    status = PsAppRuntime_InitSystem(&s_ps_app_context.runtime_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[PS] init_system failed: %d\r\n", status);
        return 1;
    }

    usleep(PS_APP_AUTO_BOOT_AUDIT_DELAY_MS * 1000U);
    PsAppVideo_PresentCapturedFrameIfReady(&s_ps_app_context.video_ctx);
    PsAppVideo_SyncDisplayFrame(&s_ps_app_context.video_ctx);
    PsAppDiag_PrintAutoBootAudit(&s_ps_app_context.diag_ctx);

    xil_printf("[PS] init done: VDMA+Audio+Regs+UART ready\r\n");
    xil_printf("[PS] boot selftest done: HDMI framebuffers cleared, audio playback configured\r\n");

    ok = xTaskCreate(PsAppMonitorTask,
                     "mon",
                     PS_APP_SYS_TASK_STACK_WORDS,
                     &s_ps_app_context.runtime_ctx,
                     PS_APP_SYS_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create monitor task\r\n");
        return 1;
    }

    ok = xTaskCreate(PsAppSaveTask,
                     "save",
                     PS_APP_SAVE_TASK_STACK_WORDS,
                     &s_ps_app_context.save_ctx,
                     PS_APP_SAVE_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create save task\r\n");
        return 1;
    }

    ok = xTaskCreate(PsAppVideoPresentTask,
                     "video",
                     PS_APP_VIDEO_TASK_STACK_WORDS,
                     &s_ps_app_context.runtime_ctx,
                     PS_APP_VIDEO_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create video task\r\n");
        return 1;
    }

    ok = xTaskCreate(PsAppConsoleTask,
                     "cons",
                     PS_APP_CONSOLE_TASK_STACK_WORDS,
                     &s_ps_app_context.console_ctx,
                     PS_APP_CONSOLE_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create console task\r\n");
        return 1;
    }

    ok = xTaskCreate(PsAppUiTask,
                     "ui",
                     PS_APP_UI_TASK_STACK_WORDS,
                     &s_ps_app_context.ui_ctx,
                     PS_APP_UI_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create ui task\r\n");
        return 1;
    }

    vTaskStartScheduler();

    for (;;) {
        usleep(1000000U);
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    xil_printf("\r\n[RTOS][FATAL] stack overflow task=%s\r\n",
               (pcTaskName != NULL) ? pcTaskName : "(null)");
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void) {
    xil_printf("\r\n[RTOS][FATAL] malloc failed free=%u min=%u\r\n",
               (unsigned int)xPortGetFreeHeapSize(),
               (unsigned int)xPortGetMinimumEverFreeHeapSize());
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationAssert(const char *pcFile, uint32_t ulLine) {
    xil_printf("\r\n[RTOS][FATAL] assert %s:%lu\r\n",
               (pcFile != NULL) ? pcFile : "(null)",
               (unsigned long)ulLine);
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
