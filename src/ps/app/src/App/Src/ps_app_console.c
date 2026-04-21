#include "App/Src/ps_app_console_internal.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"

int PsAppConsole_ParseOnOff(const char *s, u32 *value_out) {
    if ((s == NULL) || (value_out == NULL)) {
        return -1;
    }
    if ((strcmp(s, "on") == 0) || (strcmp(s, "1") == 0)) {
        *value_out = 1U;
        return 0;
    }
    if ((strcmp(s, "off") == 0) || (strcmp(s, "0") == 0)) {
        *value_out = 0U;
        return 0;
    }
    return -1;
}

int PsAppConsole_ParseU32Value(const char *s, u32 *value_out) {
    char *end_ptr;
    unsigned long value;

    if ((s == NULL) || (value_out == NULL)) {
        return -1;
    }

    value = strtoul(s, &end_ptr, 0);
    if ((end_ptr == s) || ((end_ptr != NULL) && (*end_ptr != '\0'))) {
        return -1;
    }

    *value_out = (u32)value;
    return 0;
}

const char *PsAppConsole_BiosModeName(u8 bios_mode) {
    switch ((PsAppBiosMode)bios_mode) {
        case PS_APP_BIOS_MODE_EXTERNAL:
            return "external";
        case PS_APP_BIOS_MODE_FALLBACK:
            return "fallback";
        case PS_APP_BIOS_MODE_INTERNAL:
        default:
            return "internal";
    }
}

static int PsAppConsole_HexNibble(char ch) {
    if ((ch >= '0') && (ch <= '9')) {
        return (int)(ch - '0');
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        return 10 + (int)(ch - 'a');
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        return 10 + (int)(ch - 'A');
    }
    return -1;
}

int PsAppConsole_ParseHexBytes(const char *text,
                               u8 *out_buf,
                               u32 out_buf_size,
                               u32 *out_len) {
    int high_nibble;
    int nibble;
    u32 count;
    char ch;

    if ((text == NULL) || (out_buf == NULL) || (out_len == NULL)) {
        return -1;
    }

    high_nibble = -1;
    count = 0U;
    while ((ch = *text++) != '\0') {
        if ((ch == ' ') || (ch == ':') || (ch == '-') || (ch == '_')) {
            continue;
        }

        nibble = PsAppConsole_HexNibble(ch);
        if (nibble < 0) {
            return -1;
        }

        if (high_nibble < 0) {
            high_nibble = nibble;
        } else {
            if (count >= out_buf_size) {
                return -1;
            }
            out_buf[count++] = (u8)(((u8)high_nibble << 4) | (u8)nibble);
            high_nibble = -1;
        }
    }

    if (high_nibble >= 0) {
        return -1;
    }
    if (count == 0U) {
        return -1;
    }

    *out_len = count;
    return 0;
}

static void PsAppConsole_PrintHelp(void) {
    xil_printf("Commands:\r\n");
    xil_printf("  help\r\n");

    PsAppConsole_PrintDiagnosticHelp();

    xil_printf("  input status\r\n");
    xil_printf("  input inject <hex>   (xinput report hex, allow separators : - _ space)\r\n");
    xil_printf("  input detach\r\n");
    xil_printf("  usb status\r\n");
    xil_printf("  usb kick\r\n");
    xil_printf("  usb portreset\r\n");
    xil_printf("  usb ulpi\r\n");
    xil_printf("  usb vbus on|off\r\n");
    xil_printf("  usb rumble <large 0-255> <small 0-255>\r\n");

    PsAppConsole_PrintRuntimeHelp();
    PsAppConsole_PrintMediaHelp();
}

static void PsAppConsole_ProcessLine(PsAppConsoleContext *ctx, char *line) {
    char *cmd;

    if ((ctx == NULL) || (line == NULL)) {
        return;
    }

    cmd = strtok(line, " \t");
    if (cmd == NULL) {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        PsAppConsole_PrintHelp();
        return;
    }

    if (PsAppConsole_HandleDiagnosticCommands(ctx, cmd) != 0U) {
        return;
    }

    if (PsAppConsole_HandleIoCommands(ctx, cmd) != 0U) {
        return;
    }

    if (PsAppConsole_HandleRuntimeCommands(ctx, cmd) != 0U) {
        return;
    }

    if (PsAppConsole_HandleMediaCommands(ctx, cmd) != 0U) {
        return;
    }

    xil_printf("[CMD] unknown: %s\r\n", cmd);
}

void PsAppConsoleTask(void *arg) {
    PsAppConsoleContext *ctx = (PsAppConsoleContext *)arg;
    char line[192];
    u32 len;
    u32 idle_cycles;

    len = 0U;
    idle_cycles = 0U;
    memset(line, 0, sizeof(line));

    xil_printf("[PS] Console ready, type 'help'\r\n> ");

    for (;;) {
        u8 ch;
        u32 n;

        n = XUartPs_Recv(ctx->uart, &ch, 1U);
        if (n == 1U) {
            idle_cycles = 0U;
            if ((ch == '\r') || (ch == '\n')) {
                xil_printf("\r\n");
                line[len] = '\0';
                PsAppConsole_ProcessLine(ctx, line);
                len = 0U;
                memset(line, 0, sizeof(line));
                xil_printf("> ");
            } else if ((ch == 0x08U) || (ch == 0x7FU)) {
                if (len > 0U) {
                    len--;
                    line[len] = '\0';
                    xil_printf("\b \b");
                }
            } else if ((ch >= 32U) && (ch < 127U)) {
                if (len < (sizeof(line) - 1U)) {
                    line[len++] = (char)ch;
                    xil_printf("%c", ch);
                }
            }
        } else {
            idle_cycles++;
            if (idle_cycles >= 100U) {
                /* Defensive recovery: if RX/TX got disabled by unexpected side effects,
                 * periodically re-enable UART and reset RX timeout path. */
                XUartPs_EnableUart(ctx->uart);
                XUartPs_SetOptions(ctx->uart,
                                   (u16)(XUARTPS_OPTION_RESET_RX | XUARTPS_OPTION_RESET_TMOUT));
                idle_cycles = 0U;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
