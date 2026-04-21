#include "UsbHost/Src/ps_app_usbhost_impl_internal.h"

#include "sleep.h"
#include "xil_io.h"
#include "xil_printf.h"

static void PsAppUsbHost_DelayMsBootSafe(u32 delay_ms)
{
    usleep((unsigned int)(delay_ms * 1000U));
}

static void PsAppUsbHost_EnsureHostRun(UINTPTR base_addr)
{
    u32 cmd;

    cmd = Xil_In32(base_addr + XUSBPS_CMD_OFFSET);
    cmd |= (XUSBPS_CMD_RS_MASK | XUSBPS_CMD_PSE_MASK | XUSBPS_CMD_ASE_MASK);
    Xil_Out32(base_addr + XUSBPS_CMD_OFFSET, cmd);
}

void PsAppUsbHost_ForceSlcrUsb0Config(void)
{
    static const u32 s_ps_app_usbhost_ulpi_mio28_to_39_cfg[12] = {
        0x00001204U, 0x00001205U, 0x00001204U, 0x00001205U,
        0x00001204U, 0x00001204U, 0x00001204U, 0x00001204U,
        0x00001205U, 0x00001204U, 0x00001204U, 0x00001204U
    };
    u32 reg;
    u32 idx;

    Xil_Out32(PS_APP_USBHOST_SLCR_UNLOCK_ADDR, PS_APP_USBHOST_SLCR_UNLOCK_CODE);

    reg = Xil_In32(PS_APP_USBHOST_SLCR_APER_CLK_CTRL_ADDR);
    reg |= (PS_APP_USBHOST_APER_USB0_CPU_1XCLKACT_MASK |
            PS_APP_USBHOST_APER_GPIO_CPU_1XCLKACT_MASK);
    Xil_Out32(PS_APP_USBHOST_SLCR_APER_CLK_CTRL_ADDR, reg);

    reg = Xil_In32(PS_APP_USBHOST_SLCR_USB0_CLK_CTRL_ADDR);
    reg |= PS_APP_USBHOST_USB0_CLKACT_MASK;
    Xil_Out32(PS_APP_USBHOST_SLCR_USB0_CLK_CTRL_ADDR, reg);

    reg = Xil_In32(PS_APP_USBHOST_SLCR_USB_RST_CTRL_ADDR);
    reg |= PS_APP_USBHOST_USB_RST_CTRL_MASK;
    Xil_Out32(PS_APP_USBHOST_SLCR_USB_RST_CTRL_ADDR, reg);
    PsAppUsbHost_DelayMsBootSafe(1U);
    reg &= ~PS_APP_USBHOST_USB_RST_CTRL_MASK;
    Xil_Out32(PS_APP_USBHOST_SLCR_USB_RST_CTRL_ADDR, reg);

    for (idx = 0U; idx < 12U; ++idx) {
        Xil_Out32(PS_APP_USBHOST_MIO28_CFG_ADDR + (idx * 4U),
                  s_ps_app_usbhost_ulpi_mio28_to_39_cfg[idx]);
    }

    Xil_Out32(PS_APP_USBHOST_MIO46_CFG_ADDR, 0x00001200U);
    Xil_Out32(PS_APP_USBHOST_MIO47_CFG_ADDR, 0x00001201U);

    Xil_Out32(PS_APP_USBHOST_SLCR_LOCK_ADDR, PS_APP_USBHOST_SLCR_LOCK_CODE);
}

#if defined(XPAR_XGPIOPS_0_BASEADDR)
static void PsAppUsbHost_SetPhyResetLevel(u8 deassert_high)
{
    u32 dirm;
    u32 oen;
    const u32 bit = PS_APP_USBHOST_PHY_RESET_BIT_BANK1;
    const u32 lsw_mask = bit & 0xFFFFU;
    u32 mask_data;

    dirm = Xil_In32(PS_APP_USBHOST_GPIO_BASE_ADDR + PS_APP_USBHOST_GPIO_DIRM_1_OFFSET);
    dirm |= bit;
    Xil_Out32(PS_APP_USBHOST_GPIO_BASE_ADDR + PS_APP_USBHOST_GPIO_DIRM_1_OFFSET, dirm);

    oen = Xil_In32(PS_APP_USBHOST_GPIO_BASE_ADDR + PS_APP_USBHOST_GPIO_OEN_1_OFFSET);
    oen |= bit;
    Xil_Out32(PS_APP_USBHOST_GPIO_BASE_ADDR + PS_APP_USBHOST_GPIO_OEN_1_OFFSET, oen);

    mask_data = ((~lsw_mask & 0xFFFFU) << 16) | ((deassert_high != 0U) ? lsw_mask : 0U);
    Xil_Out32(PS_APP_USBHOST_GPIO_BASE_ADDR + PS_APP_USBHOST_GPIO_MASK_1_LSW_OFF, mask_data);
}

void PsAppUsbHost_PulsePhyReset(void)
{
    PsAppUsbHost_SetPhyResetLevel(1U);
    PsAppUsbHost_DelayMsBootSafe(1U);
    PsAppUsbHost_SetPhyResetLevel(0U);
    PsAppUsbHost_DelayMsBootSafe(PS_APP_USBHOST_PHY_RESET_ASSERT_MS);
    PsAppUsbHost_SetPhyResetLevel(1U);
    PsAppUsbHost_DelayMsBootSafe(PS_APP_USBHOST_PHY_RESET_RELEASE_MS);
}
#else
void PsAppUsbHost_PulsePhyReset(void)
{
}
#endif

static XStatus PsAppUsbHost_ControllerSoftReset(UINTPTR base_addr)
{
    u32 cmd;
    u32 timeout_iter = PS_APP_USBHOST_ULPI_TIMEOUT_ITER;

    cmd = Xil_In32(base_addr + XUSBPS_CMD_OFFSET);
    cmd |= XUSBPS_CMD_RST_MASK;
    Xil_Out32(base_addr + XUSBPS_CMD_OFFSET, cmd);

    while (timeout_iter > 0U) {
        cmd = Xil_In32(base_addr + XUSBPS_CMD_OFFSET);
        if ((cmd & XUSBPS_CMD_RST_MASK) == 0U) {
            return XST_SUCCESS;
        }
        timeout_iter--;
    }

    return XST_FAILURE;
}

static XStatus PsAppUsbHost_RecoverUlpiPath(PsAppUsbHostImplContext *impl_ctx,
                                            UINTPTR base_addr,
                                            const char *tag)
{
    XStatus status;
    u8 runtime_initialized;
    u32 timeout_iter;
    u32 ulpi_view;

    if (impl_ctx == NULL) {
        return XST_FAILURE;
    }
    if (impl_ctx->ulpi_recovering != 0U) {
        return XST_FAILURE;
    }

    runtime_initialized =
        ((impl_ctx->public_context_ptr != NULL) &&
         (impl_ctx->public_context_ptr->state != NULL) &&
         (impl_ctx->public_context_ptr->state->initialized != 0U)) ? 1U : 0U;

    impl_ctx->ulpi_recovering = 1U;
    xil_printf("[USBH] ulpi recover start (%s)\r\n", (tag != NULL) ? tag : "unknown");

    PsAppUsbHost_ForceSlcrUsb0Config();
    PsAppUsbHost_PulsePhyReset();

    if (runtime_initialized == 0U) {
        status = PsAppUsbHost_ControllerSoftReset(base_addr);
        if (status != XST_SUCCESS) {
            xil_printf("[USBH] ulpi recover failed: controller reset timeout\r\n");
            impl_ctx->ulpi_recovering = 0U;
            return XST_FAILURE;
        }
    } else {
        Xil_Out32(base_addr + XUSBPS_ULPIVIEW_OFFSET,
                  XUSBPS_ULPIVIEW_WU_MASK | XUSBPS_ULPIVIEW_RUN_MASK);
        timeout_iter = PS_APP_USBHOST_ULPI_TIMEOUT_ITER;
        while (timeout_iter > 0U) {
            ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
            if ((ulpi_view & XUSBPS_ULPIVIEW_RUN_MASK) == 0U) {
                break;
            }
            timeout_iter--;
        }
    }

    Xil_Out32(base_addr + XUSBPS_ISR_OFFSET, 0xFFFFFFFFU);
    PsAppUsbHost_ApplyPortKick(base_addr);
    PsAppUsbHost_EnsureHostRun(base_addr);

    ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
    if ((ulpi_view & XUSBPS_ULPIVIEW_RUN_MASK) != 0U) {
        xil_printf("[USBH] ulpi recover failed: viewport still busy\r\n");
        impl_ctx->ulpi_recovering = 0U;
        return XST_FAILURE;
    }

    xil_printf("[USBH] ulpi recover done\r\n");
    impl_ctx->ulpi_recovering = 0U;
    return XST_SUCCESS;
}

static XStatus PsAppUsbHost_UlpiWaitClear(PsAppUsbHostImplContext *impl_ctx,
                                          UINTPTR base_addr,
                                          u32 mask,
                                          u32 timeout_iter)
{
    u32 ulpi_view;
    u32 retry_iter = timeout_iter;

    while (retry_iter > 0U) {
        ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
        if ((ulpi_view & mask) == 0U) {
            return XST_SUCCESS;
        }
        retry_iter--;
    }

    if ((mask & XUSBPS_ULPIVIEW_RUN_MASK) != 0U) {
        if (PsAppUsbHost_RecoverUlpiPath(impl_ctx, base_addr, "run-stuck") == XST_SUCCESS) {
            retry_iter = timeout_iter;
            while (retry_iter > 0U) {
                ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
                if ((ulpi_view & mask) == 0U) {
                    return XST_SUCCESS;
                }
                retry_iter--;
            }
        }
    }

    return XST_FAILURE;
}

static XStatus PsAppUsbHost_UlpiWaitIdle(PsAppUsbHostImplContext *impl_ctx,
                                         UINTPTR base_addr,
                                         u32 timeout_iter)
{
    return PsAppUsbHost_UlpiWaitClear(impl_ctx, base_addr, XUSBPS_ULPIVIEW_RUN_MASK, timeout_iter);
}

static XStatus PsAppUsbHost_UlpiWakeup(PsAppUsbHostImplContext *impl_ctx, UINTPTR base_addr)
{
    u32 ulpi_view;

    if (PsAppUsbHost_UlpiWaitIdle(impl_ctx, base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
    if ((ulpi_view & XUSBPS_ULPIVIEW_SS_MASK) != 0U) {
        return XST_SUCCESS;
    }

    Xil_Out32(base_addr + XUSBPS_ULPIVIEW_OFFSET, XUSBPS_ULPIVIEW_WU_MASK | XUSBPS_ULPIVIEW_RUN_MASK);
    if (PsAppUsbHost_UlpiWaitIdle(impl_ctx, base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
    if ((ulpi_view & XUSBPS_ULPIVIEW_SS_MASK) == 0U) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_UlpiRead(PsAppUsbHostImplContext *impl_ctx, UINTPTR base_addr, u8 reg_addr, u8 *value_out)
{
    u32 ulpi_view;

    if (value_out == NULL) {
        return XST_INVALID_PARAM;
    }
    if (impl_ctx == NULL) {
        return XST_FAILURE;
    }
    if (PsAppUsbHost_UlpiWakeup(impl_ctx, base_addr) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = (((u32)reg_addr << PS_APP_USBHOST_ULPI_ADDR_SHIFT) & XUSBPS_ULPIVIEW_ADDR_MASK) |
                XUSBPS_ULPIVIEW_RUN_MASK;
    Xil_Out32(base_addr + XUSBPS_ULPIVIEW_OFFSET, ulpi_view);

    if (PsAppUsbHost_UlpiWaitIdle(impl_ctx, base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
    *value_out = (u8)((ulpi_view & XUSBPS_ULPIVIEW_DATRD_MASK) >> PS_APP_USBHOST_ULPI_DATRD_SHIFT);
    return XST_SUCCESS;
}

static XStatus PsAppUsbHost_UlpiWrite(PsAppUsbHostImplContext *impl_ctx, UINTPTR base_addr, u8 reg_addr, u8 value)
{
    u32 ulpi_view;

    if (impl_ctx == NULL) {
        return XST_FAILURE;
    }
    if (PsAppUsbHost_UlpiWakeup(impl_ctx, base_addr) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = (((u32)reg_addr << PS_APP_USBHOST_ULPI_ADDR_SHIFT) & XUSBPS_ULPIVIEW_ADDR_MASK) |
                ((u32)value & XUSBPS_ULPIVIEW_DATWR_MASK) |
                XUSBPS_ULPIVIEW_RW_MASK |
                XUSBPS_ULPIVIEW_RUN_MASK;
    Xil_Out32(base_addr + XUSBPS_ULPIVIEW_OFFSET, ulpi_view);

    if (PsAppUsbHost_UlpiWaitIdle(impl_ctx, base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_SetVbusDriveByBase(PsAppUsbHostImplContext *impl_ctx, UINTPTR base_addr, u8 enable)
{
    u8 otg_ctrl;
    u32 otgcsr_ctrl;
    u8 ulpi_ready = 1U;

    if (impl_ctx == NULL) {
        return XST_FAILURE;
    }

    if (PsAppUsbHost_UlpiRead(impl_ctx, base_addr, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, &otg_ctrl) != XST_SUCCESS) {
        (void)PsAppUsbHost_RecoverUlpiPath(impl_ctx, base_addr, "set-vbus");
        if (PsAppUsbHost_UlpiRead(impl_ctx, base_addr, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, &otg_ctrl) != XST_SUCCESS) {
            ulpi_ready = 0U;
        }
    }

    if (ulpi_ready != 0U) {
        if (enable != 0U) {
            otg_ctrl |= (u8)(PS_APP_USBHOST_ULPI_OTG_DRVVBUS | PS_APP_USBHOST_ULPI_OTG_DRVVBUS_EXT);
            otg_ctrl &= (u8)~(PS_APP_USBHOST_ULPI_OTG_DISCHRGVBUS | PS_APP_USBHOST_ULPI_OTG_CHRGVBUS);
        } else {
            otg_ctrl &= (u8)~(PS_APP_USBHOST_ULPI_OTG_DRVVBUS | PS_APP_USBHOST_ULPI_OTG_DRVVBUS_EXT);
            otg_ctrl &= (u8)~(PS_APP_USBHOST_ULPI_OTG_DISCHRGVBUS | PS_APP_USBHOST_ULPI_OTG_CHRGVBUS);
        }

        if (PsAppUsbHost_UlpiWrite(impl_ctx, base_addr, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, otg_ctrl) != XST_SUCCESS) {
            ulpi_ready = 0U;
        }
    }

    if (ulpi_ready == 0U) {
        xil_printf("[USBH] vbus: ulpi write unavailable, fallback to OTGCSR/PORTSC\r\n");
    }

    otgcsr_ctrl = Xil_In32(base_addr + XUSBPS_OTGCSR_OFFSET) & PS_APP_USBHOST_OTGSC_CTRL_BITS;
    if (enable != 0U) {
        otgcsr_ctrl |= XUSBPS_OTGSC_VC_MASK;
        otgcsr_ctrl &= ~XUSBPS_OTGSC_VD_MASK;
    } else {
        otgcsr_ctrl &= ~(XUSBPS_OTGSC_VC_MASK | XUSBPS_OTGSC_VD_MASK);
    }
    Xil_Out32(base_addr + XUSBPS_OTGCSR_OFFSET, otgcsr_ctrl);

    if (enable != 0U) {
        u32 portsc;

        portsc = Xil_In32(base_addr + XUSBPS_PORTSCR1_OFFSET);
        portsc |= (XUSBPS_PORTSCR_PP_MASK | PS_APP_USBHOST_PORTSC_CHANGE_BITS);
        portsc &= ~(XUSBPS_PORTSCR_PHCD_MASK | XUSBPS_PORTSCR_SUSP_MASK | XUSBPS_PORTSCR_FPR_MASK);
        Xil_Out32(base_addr + XUSBPS_PORTSCR1_OFFSET, portsc);
    }

    return XST_SUCCESS;
}

void PsAppUsbHost_ApplyPortKick(UINTPTR base_addr)
{
    u32 mode;
    u32 portsc;

    mode = Xil_In32(base_addr + XUSBPS_MODE_OFFSET);
    mode &= ~XUSBPS_MODE_CM_MASK;
    mode |= XUSBPS_MODE_CM_HOST_MASK;
    Xil_Out32(base_addr + XUSBPS_MODE_OFFSET, mode);

    portsc = Xil_In32(base_addr + XUSBPS_PORTSCR1_OFFSET);
    portsc &= ~(XUSBPS_PORTSCR_PHCD_MASK | XUSBPS_PORTSCR_SUSP_MASK | XUSBPS_PORTSCR_FPR_MASK);
    portsc |= XUSBPS_PORTSCR_PP_MASK;
    portsc |= PS_APP_USBHOST_PORTSC_CHANGE_BITS;
    Xil_Out32(base_addr + XUSBPS_PORTSCR1_OFFSET, portsc);

    PsAppUsbHost_EnsureHostRun(base_addr);
}

void PsAppUsbHost_PrintSlcrSummary(void)
{
    u32 aper_clk;
    u32 usb0_clk;
    u32 usb_rst;
    u32 mio28;
    u32 mio39;
    u32 mio46;

    aper_clk = Xil_In32(PS_APP_USBHOST_SLCR_APER_CLK_CTRL_ADDR);
    usb0_clk = Xil_In32(PS_APP_USBHOST_SLCR_USB0_CLK_CTRL_ADDR);
    usb_rst = Xil_In32(PS_APP_USBHOST_SLCR_USB_RST_CTRL_ADDR);
    mio28 = Xil_In32(PS_APP_USBHOST_MIO28_CFG_ADDR);
    mio39 = Xil_In32(PS_APP_USBHOST_MIO28_CFG_ADDR + (11U * 4U));
    mio46 = Xil_In32(PS_APP_USBHOST_MIO46_CFG_ADDR);

    xil_printf("[USBH] slcr aper=0x%08x usb0_clk=0x%08x usb_rst=0x%08x mio28=0x%08x mio39=0x%08x mio46=0x%08x\r\n",
               (unsigned int)aper_clk,
               (unsigned int)usb0_clk,
               (unsigned int)usb_rst,
               (unsigned int)mio28,
               (unsigned int)mio39,
               (unsigned int)mio46);
}
