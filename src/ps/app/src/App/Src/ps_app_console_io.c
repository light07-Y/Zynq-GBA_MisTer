#include "App/Src/ps_app_console_internal.h"

#include <string.h>

#include "Input/Inc/ps_app_input.h"
#include "UsbHost/Inc/ps_app_usbhost.h"
#include "xil_printf.h"

static void PsAppConsole_PrintXinputDiag(PsAppRuntimeContext *app) {
    const PsAppUsbHostState *usb_state;
    const PsAppInputState *input_state;

    if (app == NULL) {
        return;
    }

    usb_state = app->usb_host;
    input_state = app->input;

    xil_printf("[XDIAG] ===== BEGIN =====\r\n");
    xil_printf("[XDIAG] build=%s %s\r\n", __DATE__, __TIME__);
    if (usb_state != NULL) {
        xil_printf("[XDIAG] usb quick present=%u active=%u vid=0x%04x pid=0x%04x intf=%u cls=%02x/%02x/%02x in=0x%02x out=0x%02x\r\n",
                   (unsigned int)usb_state->device_present,
                   (unsigned int)usb_state->xbox_interface_active,
                   (unsigned int)usb_state->vendor_id,
                   (unsigned int)usb_state->product_id,
                   (unsigned int)usb_state->interface_number,
                   (unsigned int)usb_state->interface_class,
                   (unsigned int)usb_state->interface_subclass,
                   (unsigned int)usb_state->interface_protocol,
                   (unsigned int)usb_state->ep_in_addr,
                   (unsigned int)usb_state->ep_out_addr);
    }
    if (input_state != NULL) {
        xil_printf("[XDIAG] input quick active=%u valid=%u reports=%u parse_err=%u unsupported=%u last_len=%u\r\n",
                   (unsigned int)input_state->active,
                   (unsigned int)input_state->report_valid,
                   (unsigned int)input_state->report_count,
                   (unsigned int)input_state->parse_error_count,
                   (unsigned int)input_state->unsupported_report_count,
                   (unsigned int)input_state->last_report_len);
    }

    if (PsAppUsbHost_PrintStatusChecked(app->usb_host_ctx) != XST_SUCCESS) {
        xil_printf("[XDIAG] usb status unavailable\r\n");
    }
    PsAppInput_PrintStatus(app->input_ctx);
    if ((usb_state != NULL) && (usb_state->vendor_id == 0x057EU) && (usb_state->product_id == 0x2009U)) {
        xil_printf("[XDIAG] tip: detected switch-hid profile(057E:2009); this firmware has fallback parser now\r\n");
    } else {
        xil_printf("[XDIAG] tip: G30S TE please switch to XInput(View+Menu), replug receiver, rerun xdiag\r\n");
    }
    xil_printf("[XDIAG] ===== END =====\r\n");
}

u8 PsAppConsole_HandleIoCommands(PsAppConsoleContext *ctx, const char *cmd) {
    PsAppRuntimeContext *app;
    char *arg1;
    char *arg2;
    char *arg3;

    if ((ctx == NULL) || (ctx->runtime == NULL) || (cmd == NULL)) {
        return 0U;
    }

    app = ctx->runtime;

    if ((strcmp(cmd, "xdiag") == 0) || (strcmp(cmd, "xinputdiag") == 0)) {
        PsAppConsole_PrintXinputDiag(app);
        return 1U;
    }

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

        if (strcmp(arg1, "diag") == 0) {
            PsAppConsole_PrintXinputDiag(app);
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

        xil_printf("[CMD] usage: usb status|diag|kick|portreset|ulpi|vbus on|off|rumble <large 0-255> <small 0-255>\r\n");
        return 1U;
    }

    return 0U;
}
