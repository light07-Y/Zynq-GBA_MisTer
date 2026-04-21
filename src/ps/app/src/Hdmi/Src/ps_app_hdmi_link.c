#include "Hdmi/Inc/ps_app_hdmi_link.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xinterrupt_wrap.h"
#include "xparameters.h"

#if defined(XPAR_XIICPS_1_BASEADDR)
#define PS_APP_HDMI_DDC_IIC_BASEADDR XPAR_XIICPS_1_BASEADDR
#elif defined(XPAR_PSU_I2C_1_BASEADDR)
#define PS_APP_HDMI_DDC_IIC_BASEADDR XPAR_PSU_I2C_1_BASEADDR
#else
#define PS_APP_HDMI_DDC_IIC_BASEADDR 0U
#endif

#if defined(XPAR_XGPIOPS_0_INTERRUPTS) && defined(XPAR_XGPIOPS_0_INTERRUPT_PARENT)
#define PS_APP_HDMI_GPIO_INTR_ID XPAR_XGPIOPS_0_INTERRUPTS
#define PS_APP_HDMI_GPIO_INTR_PARENT XPAR_XGPIOPS_0_INTERRUPT_PARENT
#elif defined(XPAR_XGPIOPS_0_INTR) && defined(XPAR_SCUGIC_SINGLE_DEVICE_ID)
#define PS_APP_HDMI_GPIO_INTR_ID XPAR_XGPIOPS_0_INTR
#define PS_APP_HDMI_GPIO_INTR_PARENT XPAR_SCUGIC_SINGLE_DEVICE_ID
#else
#define PS_APP_HDMI_GPIO_INTR_ID 0U
#define PS_APP_HDMI_GPIO_INTR_PARENT 0U
#endif

#define PS_APP_HDMI_HPD_PIN                54U
#define PS_APP_HDMI_DDC_EDID_I2C_ADDR      0x50U
#define PS_APP_HDMI_DDC_EDID_BLOCK_SIZE    128U
#define PS_APP_HDMI_DDC_I2C_SCL_HZ         100000U
#define PS_APP_HDMI_I2C_IDLE_WAIT_LOOPS    1000000U
#define PS_APP_HDMI_POLL_CONNECTED_MS      1000U
#define PS_APP_HDMI_POLL_DISCONNECTED_MS   250U

enum {
    PS_APP_HDMI_ERR_NONE = 0U,
    PS_APP_HDMI_ERR_I2C1_CONFIG_MISSING = 1U,
    PS_APP_HDMI_ERR_I2C1_INIT_FAILED = 2U,
    PS_APP_HDMI_ERR_DDC_SEND_FAILED = 3U,
    PS_APP_HDMI_ERR_DDC_RECV_FAILED = 4U,
    PS_APP_HDMI_ERR_DDC_BUSY_TIMEOUT = 5U,
    PS_APP_HDMI_ERR_EDID_HEADER_INVALID = 6U,
    PS_APP_HDMI_ERR_EDID_CHECKSUM_INVALID = 7U
};

static const u8 s_ps_app_hdmi_link_edid_header[8] = {
    0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U
};

static u32 PsAppHdmiLink_GetTick(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return 0U;
    }
    return (u32)xTaskGetTickCount();
}

static u32 PsAppHdmiLink_PollIntervalTicks(const PsAppHdmiLinkState *state) {
    TickType_t ticks;

    ticks = pdMS_TO_TICKS((state->present_enable != 0U) ?
                              PS_APP_HDMI_POLL_CONNECTED_MS :
                              PS_APP_HDMI_POLL_DISCONNECTED_MS);
    if (ticks == 0U) {
        ticks = 1U;
    }
    return (u32)ticks;
}

static XStatus PsAppHdmiLink_WaitBusIdle(XIicPs *i2c) {
    u32 timeout;

    timeout = PS_APP_HDMI_I2C_IDLE_WAIT_LOOPS;
    while ((XIicPs_BusIsBusy(i2c) == (s32)TRUE) && (timeout != 0U)) {
        timeout--;
    }

    if (timeout == 0U) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

static XStatus PsAppHdmiLink_InitDdcI2c(PsAppHdmiLinkContext *ctx) {
    XIicPs_Config *cfg;
    XStatus status;

    if (PS_APP_HDMI_DDC_IIC_BASEADDR == 0U) {
        return XST_FAILURE;
    }

    cfg = XIicPs_LookupConfig(PS_APP_HDMI_DDC_IIC_BASEADDR);
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XIicPs_CfgInitialize(&ctx->i2c_ddc, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XIicPs_SetSClk(&ctx->i2c_ddc, PS_APP_HDMI_DDC_I2C_SCL_HZ);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XST_SUCCESS;
}

static XStatus PsAppHdmiLink_ReadEdidBlock0(PsAppHdmiLinkContext *ctx, u8 *edid_out) {
    XStatus status;
    u8 offset;

    if ((ctx == NULL) || (edid_out == NULL) || (ctx->i2c_ready == 0U)) {
        return XST_FAILURE;
    }

    offset = 0U;
    status = PsAppHdmiLink_WaitBusIdle(&ctx->i2c_ddc);
    if (status != XST_SUCCESS) {
        return (XStatus)PS_APP_HDMI_ERR_DDC_BUSY_TIMEOUT;
    }

    status = XIicPs_SetOptions(&ctx->i2c_ddc, XIICPS_REP_START_OPTION);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XIicPs_MasterSendPolled(&ctx->i2c_ddc, &offset, 1, PS_APP_HDMI_DDC_EDID_I2C_ADDR);
    (void)XIicPs_ClearOptions(&ctx->i2c_ddc, XIICPS_REP_START_OPTION);
    if (status != XST_SUCCESS) {
        return (XStatus)PS_APP_HDMI_ERR_DDC_SEND_FAILED;
    }

    status = XIicPs_MasterRecvPolled(&ctx->i2c_ddc,
                                     edid_out,
                                     PS_APP_HDMI_DDC_EDID_BLOCK_SIZE,
                                     PS_APP_HDMI_DDC_EDID_I2C_ADDR);
    if (status != XST_SUCCESS) {
        return (XStatus)PS_APP_HDMI_ERR_DDC_RECV_FAILED;
    }

    status = PsAppHdmiLink_WaitBusIdle(&ctx->i2c_ddc);
    if (status != XST_SUCCESS) {
        return (XStatus)PS_APP_HDMI_ERR_DDC_BUSY_TIMEOUT;
    }

    return XST_SUCCESS;
}

static u8 PsAppHdmiLink_IsEdidHeaderValid(const u8 *edid) {
    if (edid == NULL) {
        return 0U;
    }
    return (memcmp(edid, s_ps_app_hdmi_link_edid_header, sizeof(s_ps_app_hdmi_link_edid_header)) == 0) ? 1U : 0U;
}

static u8 PsAppHdmiLink_IsEdidChecksumValid(const u8 *edid) {
    u32 idx;
    u32 sum;

    if (edid == NULL) {
        return 0U;
    }

    sum = 0U;
    for (idx = 0U; idx < PS_APP_HDMI_DDC_EDID_BLOCK_SIZE; ++idx) {
        sum += (u32)edid[idx];
    }
    return ((sum & 0xFFU) == 0U) ? 1U : 0U;
}

static u8 PsAppHdmiLink_ParsePreferredTiming(const u8 *edid, u16 *hact, u16 *vact) {
    const u8 *dtd0;
    u16 pixel_clock_10khz;
    u16 h_active;
    u16 v_active;

    if ((edid == NULL) || (hact == NULL) || (vact == NULL)) {
        return 0U;
    }

    dtd0 = &edid[54];
    pixel_clock_10khz = (u16)dtd0[0] | ((u16)dtd0[1] << 8);
    if (pixel_clock_10khz == 0U) {
        return 0U;
    }

    h_active = (u16)dtd0[2] | (((u16)dtd0[4] & 0xF0U) << 4);
    v_active = (u16)dtd0[5] | (((u16)dtd0[7] & 0xF0U) << 4);

    *hact = h_active;
    *vact = v_active;
    return 1U;
}

static void PsAppHdmiLink_ManufacturerToAscii(u16 vendor_id, char out_code[4]) {
    out_code[0] = (char)('A' + (char)(((vendor_id >> 10) & 0x1FU) - 1U));
    out_code[1] = (char)('A' + (char)(((vendor_id >> 5) & 0x1FU) - 1U));
    out_code[2] = (char)('A' + (char)((vendor_id & 0x1FU) - 1U));
    out_code[3] = '\0';
}

static u8 PsAppHdmiLink_Has640x480p60InStandardTimings(const u8 *edid) {
    u32 idx;

    for (idx = 0U; idx < 8U; ++idx) {
        u32 base = 38U + (idx * 2U);
        u8 b0 = edid[base];
        u8 b1 = edid[base + 1U];
        u16 hact;
        u16 vact;
        u16 aspect;
        u16 refresh;

        if ((b0 == 0x01U) && (b1 == 0x01U)) {
            continue;
        }

        hact = (u16)((u16)b0 + 31U) * 8U;
        aspect = (u16)((b1 >> 6) & 0x03U);
        refresh = (u16)(60U + (u16)(b1 & 0x3FU));

        switch (aspect) {
            case 0U:
                vact = (u16)((hact * 10U) / 16U);
                break;
            case 1U:
                vact = (u16)((hact * 3U) / 4U);
                break;
            case 2U:
                vact = (u16)((hact * 4U) / 5U);
                break;
            default:
                vact = (u16)((hact * 9U) / 16U);
                break;
        }

        if ((hact == 640U) && (vact == 480U) && (refresh == 60U)) {
            return 1U;
        }
    }

    return 0U;
}

static u8 PsAppHdmiLink_EdidClaims640x480p60(const u8 *edid,
                                             u8 preferred_valid,
                                             u16 preferred_h,
                                             u16 preferred_v) {
    if (edid == NULL) {
        return 0U;
    }

    /* EDID byte35 bit5 indicates 640x480@60 established timing. */
    if ((edid[35] & 0x20U) != 0U) {
        return 1U;
    }

    if ((preferred_valid != 0U) && (preferred_h == 640U) && (preferred_v == 480U)) {
        return 1U;
    }

    return PsAppHdmiLink_Has640x480p60InStandardTimings(edid);
}

static u8 PsAppHdmiLink_ReadHpdLevel(PsAppHdmiLinkContext *ctx) {
    if ((ctx == NULL) || (ctx->ps_gpio == NULL)) {
        return 0U;
    }

#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    return (XGpioPs_ReadPin(ctx->ps_gpio, (u32)ctx->hpd_pin) != 0U) ? 1U : 0U;
#else
    return 0U;
#endif
}

static void PsAppHdmiLink_GpioCallback(void *callback_ref, u32 bank, u32 status) {
    PsAppHdmiLinkContext *ctx;
    u8 bank_id;
    u8 pin_in_bank;
    u32 pin_mask;

    ctx = (PsAppHdmiLinkContext *)callback_ref;
    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->ps_gpio == NULL)) {
        return;
    }

#if defined(versal)
    XGpioPs_GetBankPin(ctx->ps_gpio, (u8)ctx->hpd_pin, &bank_id, &pin_in_bank);
#else
    XGpioPs_GetBankPin((u8)ctx->hpd_pin, &bank_id, &pin_in_bank);
#endif
    pin_mask = (1UL << pin_in_bank);
    if ((bank != (u32)bank_id) || ((status & pin_mask) == 0U)) {
        return;
    }

    XGpioPs_IntrClearPin(ctx->ps_gpio, (u32)ctx->hpd_pin);
    ctx->state->hpd_pending = 1U;
}

XStatus PsAppHdmiLink_Init(PsAppHdmiLinkContext *ctx) {
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_FAILURE;
    }

    memset(&ctx->i2c_ddc, 0, sizeof(ctx->i2c_ddc));
    ctx->i2c_ready = 0U;
    ctx->hpd_pin = (u8)PS_APP_HDMI_HPD_PIN;
    ctx->last_poll_tick = 0U;

    ctx->state->initialized = 0U;
    ctx->state->irq_connected = 0U;
    ctx->state->hpd_level = 0U;
    ctx->state->present_enable = 0U;
    ctx->state->hpd_pending = 1U;
    ctx->state->edid_valid = 0U;
    ctx->state->edid_refresh_pending = 1U;
    ctx->state->blank_frame_pending = 1U;
    ctx->state->preferred_is_640x480p60 = 0U;
    ctx->state->preferred_timing_valid = 0U;
    ctx->state->preferred_vic = 0U;
    ctx->state->vendor_id = 0U;
    ctx->state->product_code = 0U;
    ctx->state->last_error = PS_APP_HDMI_ERR_NONE;
    ctx->state->last_service_tick = 0U;
    ctx->state->last_hpd_change_tick = 0U;
    memset(ctx->state->edid_block0, 0, sizeof(ctx->state->edid_block0));

    if (PS_APP_HDMI_DDC_IIC_BASEADDR == 0U) {
        ctx->state->last_error = PS_APP_HDMI_ERR_I2C1_CONFIG_MISSING;
        xil_printf("[HDMI] warning: XIICPS_1 base address is unavailable; EDID read disabled\r\n");
    } else {
        status = PsAppHdmiLink_InitDdcI2c(ctx);
        if (status != XST_SUCCESS) {
            ctx->state->last_error = PS_APP_HDMI_ERR_I2C1_INIT_FAILED;
            xil_printf("[HDMI] warning: DDC IIC1 init failed: %d\r\n", status);
        } else {
            ctx->i2c_ready = 1U;
        }
    }

    if (ctx->ps_gpio != NULL) {
#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
        XGpioPs_SetDirectionPin(ctx->ps_gpio, (u32)ctx->hpd_pin, 0U);
        XGpioPs_SetOutputEnablePin(ctx->ps_gpio, (u32)ctx->hpd_pin, 0U);
        ctx->state->hpd_level = PsAppHdmiLink_ReadHpdLevel(ctx);
        ctx->state->present_enable = ctx->state->hpd_level;
        ctx->state->edid_refresh_pending = ctx->state->hpd_level;
        ctx->state->blank_frame_pending = (ctx->state->hpd_level == 0U) ? 1U : 0U;

        if ((PS_APP_HDMI_GPIO_INTR_ID != 0U) && (PS_APP_HDMI_GPIO_INTR_PARENT != 0U)) {
            XGpioPs_SetCallbackHandler(ctx->ps_gpio, (void *)ctx, PsAppHdmiLink_GpioCallback);
            XGpioPs_SetIntrTypePin(ctx->ps_gpio, (u32)ctx->hpd_pin, XGPIOPS_IRQ_TYPE_EDGE_BOTH);
            XGpioPs_IntrDisablePin(ctx->ps_gpio, (u32)ctx->hpd_pin);
            XGpioPs_IntrClearPin(ctx->ps_gpio, (u32)ctx->hpd_pin);

            status = XSetupInterruptSystem((void *)ctx->ps_gpio,
                                           (void *)XGpioPs_IntrHandler,
                                           PS_APP_HDMI_GPIO_INTR_ID,
                                           PS_APP_HDMI_GPIO_INTR_PARENT,
                                           XINTERRUPT_DEFAULT_PRIORITY);
            if (status == XST_SUCCESS) {
                XGpioPs_IntrEnablePin(ctx->ps_gpio, (u32)ctx->hpd_pin);
                ctx->state->irq_connected = 1U;
            } else {
                xil_printf("[HDMI] warning: HPD GPIO interrupt setup failed: %d (fallback poll)\r\n",
                           status);
            }
        } else {
            xil_printf("[HDMI] warning: HPD GPIO interrupt macros missing, using poll mode\r\n");
        }
#else
        xil_printf("[HDMI] warning: XGpioPs is unavailable, HPD read disabled\r\n");
#endif
    } else {
        xil_printf("[HDMI] warning: no PS GPIO context, HPD read disabled\r\n");
    }

    ctx->state->initialized = 1U;
    PsAppHdmiLink_Service(ctx);
    return XST_SUCCESS;
}

void PsAppHdmiLink_Service(PsAppHdmiLinkContext *ctx) {
    PsAppHdmiLinkState *state;
    u8 hpd_now;
    u32 now_tick;
    u32 poll_interval;
    XStatus status;
    u8 claims_480p;
    u8 preferred_valid;
    u16 preferred_h;
    u16 preferred_v;
    char vendor_code[4];

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }
    state = ctx->state;
    if (state->initialized == 0U) {
        return;
    }

    now_tick = PsAppHdmiLink_GetTick();
    state->last_service_tick = now_tick;

    if (state->hpd_pending == 0U) {
        poll_interval = PsAppHdmiLink_PollIntervalTicks(state);
        if ((now_tick - ctx->last_poll_tick) < poll_interval) {
            return;
        }
    }
    ctx->last_poll_tick = now_tick;

    hpd_now = PsAppHdmiLink_ReadHpdLevel(ctx);
    if (hpd_now != state->hpd_level) {
        state->hpd_level = hpd_now;
        state->last_hpd_change_tick = now_tick;
        state->edid_refresh_pending = hpd_now;
        if (hpd_now != 0U) {
            state->hpd_rise_count++;
            xil_printf("[HDMI] HPD high, probing EDID over IIC1\r\n");
        } else {
            state->hpd_fall_count++;
            state->present_enable = 0U;
            state->edid_valid = 0U;
            state->blank_frame_pending = 1U;
            xil_printf("[HDMI] HPD low, pause video present\r\n");
        }
    }
    state->hpd_pending = 0U;

    if (state->hpd_level == 0U) {
        return;
    }

    if (state->edid_refresh_pending == 0U) {
        state->present_enable = 1U;
        return;
    }

    if (ctx->i2c_ready == 0U) {
        state->edid_valid = 0U;
        state->edid_refresh_pending = 0U;
        state->present_enable = 1U;
        if (state->last_error == PS_APP_HDMI_ERR_NONE) {
            state->last_error = PS_APP_HDMI_ERR_I2C1_INIT_FAILED;
        }
        xil_printf("[HDMI] warning: DDC IIC1 unavailable, keep fixed 640x480@60\r\n");
        return;
    }

    status = PsAppHdmiLink_ReadEdidBlock0(ctx, state->edid_block0);
    if (status != XST_SUCCESS) {
        state->edid_valid = 0U;
        state->edid_read_fail_count++;
        state->edid_refresh_pending = 0U;
        state->present_enable = 1U;
        state->last_error = (u32)status;
        xil_printf("[HDMI] warning: EDID read failed (%d), keep fixed 640x480@60\r\n", status);
        return;
    }

    if (PsAppHdmiLink_IsEdidHeaderValid(state->edid_block0) == 0U) {
        state->edid_valid = 0U;
        state->edid_read_fail_count++;
        state->edid_refresh_pending = 0U;
        state->present_enable = 1U;
        state->last_error = PS_APP_HDMI_ERR_EDID_HEADER_INVALID;
        xil_printf("[HDMI] warning: EDID header invalid, keep fixed 640x480@60\r\n");
        return;
    }

    if (PsAppHdmiLink_IsEdidChecksumValid(state->edid_block0) == 0U) {
        state->edid_valid = 0U;
        state->edid_read_fail_count++;
        state->edid_refresh_pending = 0U;
        state->present_enable = 1U;
        state->last_error = PS_APP_HDMI_ERR_EDID_CHECKSUM_INVALID;
        xil_printf("[HDMI] warning: EDID checksum invalid, keep fixed 640x480@60\r\n");
        return;
    }

    preferred_h = 0U;
    preferred_v = 0U;
    preferred_valid = PsAppHdmiLink_ParsePreferredTiming(state->edid_block0, &preferred_h, &preferred_v);
    claims_480p = PsAppHdmiLink_EdidClaims640x480p60(state->edid_block0,
                                                     preferred_valid,
                                                     preferred_h,
                                                     preferred_v);

    state->vendor_id = ((u16)state->edid_block0[8] << 8) | (u16)state->edid_block0[9];
    state->product_code = (u16)state->edid_block0[10] | ((u16)state->edid_block0[11] << 8);
    state->preferred_timing_valid = preferred_valid;
    state->preferred_is_640x480p60 =
        ((preferred_valid != 0U) && (preferred_h == 640U) && (preferred_v == 480U)) ? 1U : 0U;
    state->preferred_vic = (state->preferred_is_640x480p60 != 0U) ? 1U : 0U;
    state->edid_valid = 1U;
    state->edid_read_ok_count++;
    state->edid_refresh_pending = 0U;
    state->present_enable = 1U;
    state->last_error = PS_APP_HDMI_ERR_NONE;

    PsAppHdmiLink_ManufacturerToAscii(state->vendor_id, vendor_code);
    if (preferred_valid != 0U) {
        xil_printf("[HDMI] EDID ok vendor=%s product=0x%04x preferred=%ux%u\r\n",
                   vendor_code,
                   (unsigned int)state->product_code,
                   (unsigned int)preferred_h,
                   (unsigned int)preferred_v);
    } else {
        xil_printf("[HDMI] EDID ok vendor=%s product=0x%04x preferred=unknown\r\n",
                   vendor_code,
                   (unsigned int)state->product_code);
    }

    if (claims_480p == 0U) {
        xil_printf("[HDMI] warning: EDID does not explicitly claim 640x480@60, keep fixed 640x480@60\r\n");
    }
}

u8 PsAppHdmiLink_PresentEnabled(const PsAppHdmiLinkContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return 1U;
    }
    return ctx->state->present_enable;
}
