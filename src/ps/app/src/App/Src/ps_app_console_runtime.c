#include "App/Src/ps_app_console_internal.h"

#include <string.h>

#include "xil_printf.h"

#include "Gba/Inc/ps_gba_regs.h"

static int PsAppConsole_KeyBitFromName(const char *name) {
    if (name == NULL) return -1;
    if (strcmp(name, "a") == 0) return 0;
    if (strcmp(name, "b") == 0) return 1;
    if (strcmp(name, "select") == 0) return 2;
    if (strcmp(name, "start") == 0) return 3;
    if (strcmp(name, "right") == 0) return 4;
    if (strcmp(name, "left") == 0) return 5;
    if (strcmp(name, "up") == 0) return 6;
    if (strcmp(name, "down") == 0) return 7;
    if (strcmp(name, "r") == 0) return 8;
    if (strcmp(name, "l") == 0) return 9;
    return -1;
}

void PsAppConsole_PrintRuntimeHelp(void) {
    xil_printf("  core on|off\r\n");
    xil_printf("  turbo on|off\r\n");
    xil_printf("  lock on|off\r\n");
    xil_printf("  remap on|off\r\n");
    xil_printf("  unsafe on|off\r\n");
    xil_printf("  romsafe on|off      (force ROM DDR single-beat/no-prefetch)\r\n");
    xil_printf("  key <name> on|off   (a/b/select/start/right/left/up/down/r/l)\r\n");
    xil_printf("  keymask <hex>\r\n");
    xil_printf("  btn status           (PS-side BTN4/BTN5 on MIO50/51)\r\n");
    xil_printf("  rtc status | rtc sync <unix> [uncert_s] | rtc <unix>\r\n");
    xil_printf("  cycle <dec>\r\n");
    xil_printf("  maxpak <hex>\r\n");
    xil_printf("  commit\r\n");
    xil_printf("  irqen <hex>          (bit0=vsync, bit1=error)\r\n");
    xil_printf("  irqclr <hex>\r\n");
    xil_printf("  errclr\r\n");
    xil_printf("  reset <0|1>\r\n");
    xil_printf("  savestate slot <0..3>|save|load\r\n");
    xil_printf("  rewind on|off\r\n");
    xil_printf("  cheat on|off|clear|list\r\n");
    xil_printf("  cheat add always <addr> <value> [mask]\r\n");
    xil_printf("  cheat add if <op> <addr> <compare> then <value> [mask]\r\n");
}

u8 PsAppConsole_HandleRuntimeCommands(PsAppConsoleContext *ctx, const char *cmd) {
    PsAppRuntimeContext *app;
    char *arg1;
    char *arg2;
    u32 value;

    if ((ctx == NULL) || (ctx->runtime == NULL) || (cmd == NULL)) {
        return 0U;
    }

    app = ctx->runtime;

    if (PsAppRuntimeFeature_HandleConsole(app, cmd) != 0U) {
        return 1U;
    }

    if (strcmp(cmd, "commit") == 0) {
        PsAppRuntime_ApplyShadowConfig(app);
        xil_printf("[CMD] config committed\r\n");
        return 1U;
    }

    if (strcmp(cmd, "keymask") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &value) == 0)) {
            app->config->keys = value & 0x3FFU;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] keys=0x%03x\r\n", (unsigned int)app->config->keys);
            return 1U;
        }
        xil_printf("[CMD] usage: keymask <hex>\r\n");
        return 1U;
    }

    if (strcmp(cmd, "key") == 0) {
        int bit;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        bit = PsAppConsole_KeyBitFromName(arg1);
        if ((bit >= 0) && (PsAppConsole_ParseOnOff(arg2, &value) == 0)) {
            if (value != 0U) {
                app->config->keys |= (1UL << (u32)bit);
            } else {
                app->config->keys &= ~(1UL << (u32)bit);
            }
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] key %s=%u keys=0x%03x\r\n",
                       arg1,
                       (unsigned int)value,
                       (unsigned int)app->config->keys);
            return 1U;
        }
        xil_printf("[CMD] usage: key <name> on|off\r\n");
        return 1U;
    }

    if ((strcmp(cmd, "btn") == 0) || (strcmp(cmd, "button") == 0)) {
        u32 ps_btn_mask;
        u32 btn4_raw;
        u32 btn5_raw;
        u32 bank1_data;
        u32 bank1_dir;
        u32 bank1_outen;
        u32 mio50_cfg;
        u32 mio51_cfg;

        arg1 = strtok(NULL, " \t");

        if ((arg1 == NULL) || (strcmp(arg1, "status") == 0)) {
            ps_btn_mask = PsAppRuntime_ReadPsButtonMask(app);
            PsAppRuntime_ReadPsButtonRawLevels(app,
                                               &btn4_raw,
                                               &btn5_raw,
                                               &bank1_data,
                                               &bank1_dir,
                                               &bank1_outen,
                                               &mio50_cfg,
                                               &mio51_cfg);
            xil_printf("[CMD] ps_gpio_ready=%u BTN4=%u BTN5=%u mask=0x%02x raw50=%u raw51=%u bank1=0x%08x dir1=0x%08x outen1=0x%08x mio50=0x%08x mio51=0x%08x\r\n",
                       (unsigned int)app->ps_gpio_ready,
                       (unsigned int)((ps_btn_mask & PS_APP_BTN4_MASK) != 0U),
                       (unsigned int)((ps_btn_mask & PS_APP_BTN5_MASK) != 0U),
                       (unsigned int)(ps_btn_mask & (PS_APP_BTN4_MASK | PS_APP_BTN5_MASK)),
                       (unsigned int)btn4_raw,
                       (unsigned int)btn5_raw,
                       (unsigned int)bank1_data,
                       (unsigned int)bank1_dir,
                       (unsigned int)bank1_outen,
                       (unsigned int)mio50_cfg,
                       (unsigned int)mio51_cfg);
            return 1U;
        }

        xil_printf("[CMD] usage: btn status\r\n");
        return 1U;
    }

    if ((strcmp(cmd, "core") == 0) || (strcmp(cmd, "turbo") == 0) ||
        (strcmp(cmd, "lock") == 0) || (strcmp(cmd, "remap") == 0) ||
        (strcmp(cmd, "unsafe") == 0) || (strcmp(cmd, "romsafe") == 0)) {
        u32 bit;

        arg1 = strtok(NULL, " \t");
        if (PsAppConsole_ParseOnOff(arg1, &value) != 0) {
            xil_printf("[CMD] usage: %s on|off\r\n", cmd);
            return 1U;
        }

        if (strcmp(cmd, "core") == 0) {
            bit = 0U;
        } else if (strcmp(cmd, "lock") == 0) {
            bit = 1U;
        } else if (strcmp(cmd, "turbo") == 0) {
            bit = 2U;
        } else if (strcmp(cmd, "remap") == 0) {
            bit = 5U;
        } else if (strcmp(cmd, "romsafe") == 0) {
            bit = 14U;
        } else {
            bit = 13U;
        }

        if (value != 0U) {
            app->config->ctrl |= (1UL << bit);
        } else {
            app->config->ctrl &= ~(1UL << bit);
        }
        PsAppRuntime_ApplyShadowConfig(app);
        xil_printf("[CMD] %s=%u ctrl=0x%08x\r\n",
                   cmd,
                   (unsigned int)value,
                   (unsigned int)app->config->ctrl);
        return 1U;
    }

    if (strcmp(cmd, "rtc") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &value) == 0)) {
            app->config->rtc_timestamp = value;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] rtc=0x%08x\r\n", (unsigned int)app->config->rtc_timestamp);
            return 1U;
        }
        xil_printf("[CMD] usage: rtc <hex>\r\n");
        return 1U;
    }

    if (strcmp(cmd, "cycle") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &value) == 0)) {
            app->config->cycle_precalc = value & 0xFFFFU;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] cycle=%u\r\n", (unsigned int)app->config->cycle_precalc);
            return 1U;
        }
        xil_printf("[CMD] usage: cycle <dec>\r\n");
        return 1U;
    }

    if (strcmp(cmd, "maxpak") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &value) == 0)) {
            app->config->max_pak_addr = value & 0x1FFFFFFU;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] maxpak=0x%08x\r\n", (unsigned int)app->config->max_pak_addr);
            return 1U;
        }
        xil_printf("[CMD] usage: maxpak <hex>\r\n");
        return 1U;
    }

    if (strcmp(cmd, "irqen") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &value) == 0)) {
            app->config->irq_enable = value & 0x3U;
            PsGbaRegs_SetIrqEnable(app->regs, app->config->irq_enable);
            PsGbaRegs_ClearIrqStatus(app->regs, PS_APP_IRQ_MASK_VSYNC | PS_APP_IRQ_MASK_ERROR);
            app->diag->last_runtime_irq_sts = 0U;
            xil_printf("[CMD] irqen=0x%x\r\n", (unsigned int)app->config->irq_enable);
            return 1U;
        }
        xil_printf("[CMD] usage: irqen <hex>\r\n");
        return 1U;
    }

    if (strcmp(cmd, "irqclr") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &value) == 0)) {
            PsGbaRegs_ClearIrqStatus(app->regs, value & 0x3U);
            xil_printf("[CMD] irq status cleared: 0x%x\r\n", (unsigned int)(value & 0x3U));
            return 1U;
        }
        xil_printf("[CMD] usage: irqclr <hex>\r\n");
        return 1U;
    }

    if (strcmp(cmd, "errclr") == 0) {
        PsGbaRegs_ClearErrorLatch(app->regs);
        xil_printf("[CMD] error latch clear requested\r\n");
        return 1U;
    }

    if (strcmp(cmd, "reset") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &value) == 0)) {
            PsGbaRegs_SetSwReset(app->regs, value & 0x1U);
            xil_printf("[CMD] sw_reset=%u\r\n", (unsigned int)(value & 0x1U));
            return 1U;
        }
        xil_printf("[CMD] usage: reset <0|1>\r\n");
        return 1U;
    }

    return 0U;
}
