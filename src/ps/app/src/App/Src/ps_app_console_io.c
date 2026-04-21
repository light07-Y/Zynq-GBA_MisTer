#include "App/Src/ps_app_console_internal.h"

#include <string.h>

#include "Input/Inc/ps_app_input.h"
#include "UsbHost/Inc/ps_app_usbhost.h"
#include "xil_printf.h"

u8 PsAppConsole_HandleIoCommands(PsAppConsoleContext *ctx, const char *cmd) {
    PsAppRuntimeContext *app;
    char *arg1;
    char *arg2;
    char *arg3;

    if ((ctx == NULL) || (ctx->runtime == NULL) || (cmd == NULL)) {
        return 0U;
    }

    app = ctx->runtime;

    if (strcmp(cmd, "input") == 0) {
        u8 report[PS_XINPUT_MAX_REPORT_BYTES];
        u32 report_len;

        arg1 = strtok(NULL, " \t");
        if ((arg1 == NULL) || (strcmp(arg1, "status") == 0)) {
            PsAppInput_PrintStatus(app->input_ctx);
            return 1U;
        }

        if (strcmp(arg1, "detach") == 0) {
            PsAppInput_OnUsbDetached(app->input_ctx);
            xil_printf("[CMD] input detached\r\n");
            return 1U;
        }

        if (strcmp(arg1, "inject") == 0) {
            arg2 = strtok(NULL, "\r\n");
            if ((arg2 == NULL) ||
                (PsAppConsole_ParseHexBytes(arg2,
                                            report,
                                            sizeof(report),
                                            &report_len) != 0)) {
                xil_printf("[CMD] usage: input inject <hex>\r\n");
                xil_printf("[CMD] eg: input inject 0014000000000000000000000000000000000000\r\n");
                return 1U;
            }

            if (PsAppInput_InjectRawReport(app->input_ctx, report, report_len) == XST_SUCCESS) {
                xil_printf("[CMD] input inject ok, bytes=%u\r\n", (unsigned int)report_len);
            } else {
                xil_printf("[CMD] input inject failed\r\n");
            }
            return 1U;
        }

        xil_printf("[CMD] usage: input status|inject <hex>|detach\r\n");
        return 1U;
    }

    if (strcmp(cmd, "usb") == 0) {
        u32 on_off;

        arg1 = strtok(NULL, " \t");

        if ((arg1 == NULL) || (strcmp(arg1, "status") == 0)) {
            if (PsAppUsbHost_PrintStatusChecked(app->usb_host_ctx) != XST_SUCCESS) {
                xil_printf("[CMD] usb status unavailable\r\n");
            }
            return 1U;
        }

        if (strcmp(arg1, "kick") == 0) {
            if (PsAppUsbHost_ForceRootHubScanChecked(app->usb_host_ctx) == XST_SUCCESS) {
                xil_printf("[CMD] usb kick requested\r\n");
            } else {
                xil_printf("[CMD] usb kick failed\r\n");
            }
            return 1U;
        }

        if (strcmp(arg1, "portreset") == 0) {
            if (PsAppUsbHost_PortReset(app->usb_host_ctx) == XST_SUCCESS) {
                xil_printf("[CMD] usb portreset ok\r\n");
            } else {
                xil_printf("[CMD] usb portreset failed\r\n");
            }
            return 1U;
        }

        if (strcmp(arg1, "ulpi") == 0) {
            if (PsAppUsbHost_DumpUlpi(app->usb_host_ctx) == XST_SUCCESS) {
                xil_printf("[CMD] usb ulpi ok\r\n");
            } else {
                xil_printf("[CMD] usb ulpi failed\r\n");
            }
            return 1U;
        }

        if (strcmp(arg1, "vbus") == 0) {
            arg2 = strtok(NULL, " \t");
            if (PsAppConsole_ParseOnOff(arg2, &on_off) != 0) {
                xil_printf("[CMD] usage: usb vbus on|off\r\n");
                return 1U;
            }
            if (PsAppUsbHost_SetVbusDrive(app->usb_host_ctx, (u8)on_off) == XST_SUCCESS) {
                xil_printf("[CMD] usb vbus=%u\r\n", (unsigned int)on_off);
            } else {
                xil_printf("[CMD] usb vbus set failed\r\n");
            }
            return 1U;
        }

        if (strcmp(arg1, "rumble") == 0) {
            u32 large_motor;
            u32 small_motor;

            arg2 = strtok(NULL, " \t");
            arg3 = strtok(NULL, " \t");
            if ((arg2 == NULL) || (arg3 == NULL) ||
                (PsAppConsole_ParseU32Value(arg2, &large_motor) != 0) ||
                (PsAppConsole_ParseU32Value(arg3, &small_motor) != 0) ||
                (large_motor > 255U) || (small_motor > 255U)) {
                xil_printf("[CMD] usage: usb rumble <large 0-255> <small 0-255>\r\n");
                return 1U;
            }

            if (PsAppUsbHost_SetRumble(app->usb_host_ctx, (u8)large_motor, (u8)small_motor) == XST_SUCCESS) {
                xil_printf("[CMD] usb rumble ok L=%u S=%u\r\n",
                           (unsigned int)large_motor,
                           (unsigned int)small_motor);
            } else {
                xil_printf("[CMD] usb rumble failed\r\n");
            }
            return 1U;
        }

        xil_printf("[CMD] usage: usb status|kick|portreset|ulpi|vbus on|off|rumble <large 0-255> <small 0-255>\r\n");
        return 1U;
    }

    return 0U;
}
