#include "UsbHost/Inc/ps_app_usbhost.h"

#include <string.h>

#include "sleep.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xinterrupt_wrap.h"
#include "xparameters.h"
#include "xusbps_hw.h"

#include "Input/Inc/ps_app_input.h"

#include "usb_errno.h"
#include "usb_osal.h"
#include "usb_util.h"
#include "usbh_core.h"
#include "usbh_hub.h"
#include "usbh_xbox.h"

#if defined(XPAR_XUSBPS_0_BASEADDR)
#define PS_APP_USBHOST_BASE_ADDR XPAR_XUSBPS_0_BASEADDR
#elif defined(XPAR_USB0_BASEADDR)
#define PS_APP_USBHOST_BASE_ADDR XPAR_USB0_BASEADDR
#else
#error "USB base address macro is not available in xparameters.h"
#endif

#if defined(XPAR_XUSBPS_0_INTERRUPTS) && defined(XPAR_XUSBPS_0_INTERRUPT_PARENT)
#define PS_APP_USBHOST_INTR_ID XPAR_XUSBPS_0_INTERRUPTS
#define PS_APP_USBHOST_INTR_PARENT XPAR_XUSBPS_0_INTERRUPT_PARENT
#elif defined(XPAR_USB0_INTERRUPTS) && defined(XPAR_USB0_INTERRUPT_PARENT)
#define PS_APP_USBHOST_INTR_ID XPAR_USB0_INTERRUPTS
#define PS_APP_USBHOST_INTR_PARENT XPAR_USB0_INTERRUPT_PARENT
#else
#define PS_APP_USBHOST_INTR_ID 0U
#define PS_APP_USBHOST_INTR_PARENT 0U
#endif

#define PS_APP_USBHOST_BUS_ID            0U
#define PS_APP_USBHOST_INTIN_BUFFER_SIZE 64U
#define PS_APP_USBHOST_ULPI_TIMEOUT_ITER 500000U
#define PS_APP_USBHOST_PORT_RESET_TIMEOUT_MS 100U
#define PS_APP_USBHOST_ENUM_RETRY_COOLDOWN 1000U
#define PS_APP_USBHOST_ENUM_MAX_RETRY      5U
#define PS_APP_USBHOST_PHY_RESET_ASSERT_MS 5U
#define PS_APP_USBHOST_PHY_RESET_RELEASE_MS 20U
#define PS_APP_USBHOST_8BITDO_REPORT_TIMEOUT_TICKS 1000U

#define PS_APP_USBHOST_ULPI_ADDR_SHIFT  16U
#define PS_APP_USBHOST_ULPI_DATRD_SHIFT 8U

#define PS_APP_USBHOST_ULPI_REG_VENDOR_ID_LOW   0x00U
#define PS_APP_USBHOST_ULPI_REG_VENDOR_ID_HIGH  0x01U
#define PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_LOW  0x02U
#define PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_HIGH 0x03U
#define PS_APP_USBHOST_ULPI_REG_OTG_CTRL        0x0AU
#define PS_APP_USBHOST_ULPI_REG_FUNC_CTRL       0x04U
#define PS_APP_USBHOST_ULPI_REG_IFACE_CTRL      0x07U

#define PS_APP_USBHOST_ULPI_OTG_DISCHRGVBUS (1U << 3)
#define PS_APP_USBHOST_ULPI_OTG_CHRGVBUS    (1U << 4)
#define PS_APP_USBHOST_ULPI_OTG_DRVVBUS     (1U << 5)
#define PS_APP_USBHOST_ULPI_OTG_DRVVBUS_EXT (1U << 6)

#define PS_APP_USBHOST_PORTSC_CHANGE_BITS \
    (XUSBPS_PORTSCR_CSC_MASK | XUSBPS_PORTSCR_PEC_MASK | XUSBPS_PORTSCR_OCC_MASK)

#define PS_APP_USBHOST_OTGSC_CTRL_BITS \
    (XUSBPS_OTGSC_VD_MASK | XUSBPS_OTGSC_VC_MASK | XUSBPS_OTGSC_HAAR_MASK | \
     XUSBPS_OTGSC_OT_MASK | XUSBPS_OTGSC_DP_MASK | XUSBPS_OTGSC_IDPU_MASK | \
     XUSBPS_OTGSC_HADP_MASK | XUSBPS_OTGSC_HABA_MASK)

#define PS_APP_USBHOST_SLCR_LOCK_ADDR      0xF8000004U
#define PS_APP_USBHOST_SLCR_UNLOCK_ADDR    0xF8000008U
#define PS_APP_USBHOST_SLCR_LOCK_CODE      0x0000767BU
#define PS_APP_USBHOST_SLCR_UNLOCK_CODE    0x0000DF0DU

#define PS_APP_USBHOST_SLCR_APER_CLK_CTRL_ADDR 0xF800012CU
#define PS_APP_USBHOST_SLCR_USB0_CLK_CTRL_ADDR 0xF8000130U
#define PS_APP_USBHOST_SLCR_USB_RST_CTRL_ADDR  0xF8000210U

#define PS_APP_USBHOST_APER_USB0_CPU_1XCLKACT_MASK (1U << 2U)
#define PS_APP_USBHOST_APER_GPIO_CPU_1XCLKACT_MASK (1U << 22U)
#define PS_APP_USBHOST_USB0_CLKACT_MASK            0x00000001U
#define PS_APP_USBHOST_USB_RST_CTRL_MASK           0x00000003U

#define PS_APP_USBHOST_MIO28_CFG_ADDR 0xF8000770U
#define PS_APP_USBHOST_MIO46_CFG_ADDR 0xF80007B8U
#define PS_APP_USBHOST_MIO47_CFG_ADDR 0xF80007BCU

#if defined(XPAR_XGPIOPS_0_BASEADDR)
#define PS_APP_USBHOST_GPIO_BASE_ADDR        ((UINTPTR)XPAR_XGPIOPS_0_BASEADDR)
#define PS_APP_USBHOST_GPIO_DIRM_1_OFFSET    0x00000244U
#define PS_APP_USBHOST_GPIO_OEN_1_OFFSET     0x00000248U
#define PS_APP_USBHOST_GPIO_MASK_1_LSW_OFF   0x00000008U
#define PS_APP_USBHOST_PHY_RESET_BIT_BANK1   (1U << 14U) /* MIO46 */
#endif

static PsAppUsbHostContext *g_ps_usbhost_ctx = NULL;
static struct usbh_xbox *g_ps_active_xbox = NULL;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX u8 g_ps_xbox_intin_buffer[PS_APP_USBHOST_INTIN_BUFFER_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX u8 g_ps_xbox_intout_buffer[PS_XINPUT_REPORT_SIZE_OUTPUT];
static u32 g_ps_usbhost_last_portsc = 0U;
static u8 g_ps_usbhost_force_scan_pending = 0U;
static u32 g_ps_usbhost_recover_cooldown = 0U;
static u8 g_ps_usbhost_recover_attempts = 0U;
static u8 g_ps_usbhost_ulpi_recovering = 0U;
static u32 g_ps_usbhost_no_report_ticks = 0U;

static void PsAppUsbHost_ApplyPortKick(UINTPTR base_addr);
static void PsAppUsbHost_PrintSlcrSummary(void);
static XStatus PsAppUsbHost_Send8BitDoStartupSequence(struct usbh_xbox *xbox_class);

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

static void PsAppUsbHost_ForceSlcrUsb0Config(void)
{
    static const u32 s_ulpi_mio28_to_39_cfg[12] = {
        0x00001204U, 0x00001205U, 0x00001204U, 0x00001205U,
        0x00001204U, 0x00001204U, 0x00001204U, 0x00001204U,
        0x00001205U, 0x00001204U, 0x00001204U, 0x00001204U
    };
    u32 reg;
    u32 idx;

    /* Runtime guard for stale platform/FSBL settings that break USB0 ULPI host mode. */
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
                  s_ulpi_mio28_to_39_cfg[idx]);
    }

    Xil_Out32(PS_APP_USBHOST_MIO46_CFG_ADDR, 0x00001200U); /* PHY reset GPIO */
    Xil_Out32(PS_APP_USBHOST_MIO47_CFG_ADDR, 0x00001201U); /* USB VBUS fault input */

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

static void PsAppUsbHost_PulsePhyReset(void)
{
    /* USB reset is configured active-low on MIO46; keep PHY in known-good state. */
    PsAppUsbHost_SetPhyResetLevel(1U);
    PsAppUsbHost_DelayMsBootSafe(1U);
    PsAppUsbHost_SetPhyResetLevel(0U);
    PsAppUsbHost_DelayMsBootSafe(PS_APP_USBHOST_PHY_RESET_ASSERT_MS);
    PsAppUsbHost_SetPhyResetLevel(1U);
    PsAppUsbHost_DelayMsBootSafe(PS_APP_USBHOST_PHY_RESET_RELEASE_MS);
}
#else
static void PsAppUsbHost_PulsePhyReset(void)
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

static XStatus PsAppUsbHost_RecoverUlpiPath(UINTPTR base_addr, const char *tag)
{
    XStatus status;
    u8 runtime_initialized;
    u32 timeout_iter;
    u32 ulpi_view;

    if (g_ps_usbhost_ulpi_recovering != 0U) {
        return XST_FAILURE;
    }

    runtime_initialized =
        ((g_ps_usbhost_ctx != NULL) &&
         (g_ps_usbhost_ctx->state != NULL) &&
         (g_ps_usbhost_ctx->state->initialized != 0U)) ? 1U : 0U;

    g_ps_usbhost_ulpi_recovering = 1U;
    xil_printf("[USBH] ulpi recover start (%s)\r\n", (tag != NULL) ? tag : "unknown");

    PsAppUsbHost_ForceSlcrUsb0Config();
    PsAppUsbHost_PulsePhyReset();

    if (runtime_initialized == 0U) {
        status = PsAppUsbHost_ControllerSoftReset(base_addr);
        if (status != XST_SUCCESS) {
            xil_printf("[USBH] ulpi recover failed: controller reset timeout\r\n");
            g_ps_usbhost_ulpi_recovering = 0U;
            return XST_FAILURE;
        }
    } else {
        /*
         * Do not soft-reset EHCI after host stack init; that clears queue pointers
         * and can leave host halted until a full reinit.
         */
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
        g_ps_usbhost_ulpi_recovering = 0U;
        return XST_FAILURE;
    }

    xil_printf("[USBH] ulpi recover done\r\n");
    g_ps_usbhost_ulpi_recovering = 0U;
    return XST_SUCCESS;
}

static XStatus PsAppUsbHost_UlpiWaitClear(UINTPTR base_addr, u32 mask, u32 timeout_iter)
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
        if (PsAppUsbHost_RecoverUlpiPath(base_addr, "run-stuck") == XST_SUCCESS) {
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

static XStatus PsAppUsbHost_UlpiWaitIdle(UINTPTR base_addr, u32 timeout_iter)
{
    return PsAppUsbHost_UlpiWaitClear(base_addr, XUSBPS_ULPIVIEW_RUN_MASK, timeout_iter);
}

static XStatus PsAppUsbHost_UlpiWakeup(UINTPTR base_addr)
{
    u32 ulpi_view;

    if (PsAppUsbHost_UlpiWaitIdle(base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
    if ((ulpi_view & XUSBPS_ULPIVIEW_SS_MASK) != 0U) {
        return XST_SUCCESS;
    }

    /*
     * ULPI wakeup requires RUN to trigger the viewport transaction;
     * writing WU alone can leave the viewport busy and eventually time out.
     */
    Xil_Out32(base_addr + XUSBPS_ULPIVIEW_OFFSET, XUSBPS_ULPIVIEW_WU_MASK | XUSBPS_ULPIVIEW_RUN_MASK);
    if (PsAppUsbHost_UlpiWaitIdle(base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
    if ((ulpi_view & XUSBPS_ULPIVIEW_SS_MASK) == 0U) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

static XStatus PsAppUsbHost_UlpiRead(UINTPTR base_addr, u8 reg_addr, u8 *value_out)
{
    u32 ulpi_view;

    if (value_out == NULL) {
        return XST_INVALID_PARAM;
    }

    if (PsAppUsbHost_UlpiWakeup(base_addr) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    /* ULPI viewport bit29: 0=read, 1=write. */
    ulpi_view = (((u32)reg_addr << PS_APP_USBHOST_ULPI_ADDR_SHIFT) & XUSBPS_ULPIVIEW_ADDR_MASK) |
                XUSBPS_ULPIVIEW_RUN_MASK;
    Xil_Out32(base_addr + XUSBPS_ULPIVIEW_OFFSET, ulpi_view);

    if (PsAppUsbHost_UlpiWaitIdle(base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = Xil_In32(base_addr + XUSBPS_ULPIVIEW_OFFSET);
    *value_out = (u8)((ulpi_view & XUSBPS_ULPIVIEW_DATRD_MASK) >> PS_APP_USBHOST_ULPI_DATRD_SHIFT);
    return XST_SUCCESS;
}

static XStatus PsAppUsbHost_UlpiWrite(UINTPTR base_addr, u8 reg_addr, u8 value)
{
    u32 ulpi_view;

    if (PsAppUsbHost_UlpiWakeup(base_addr) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ulpi_view = (((u32)reg_addr << PS_APP_USBHOST_ULPI_ADDR_SHIFT) & XUSBPS_ULPIVIEW_ADDR_MASK) |
                ((u32)value & XUSBPS_ULPIVIEW_DATWR_MASK) |
                XUSBPS_ULPIVIEW_RW_MASK |
                XUSBPS_ULPIVIEW_RUN_MASK;
    Xil_Out32(base_addr + XUSBPS_ULPIVIEW_OFFSET, ulpi_view);

    if (PsAppUsbHost_UlpiWaitIdle(base_addr, PS_APP_USBHOST_ULPI_TIMEOUT_ITER) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

static XStatus PsAppUsbHost_SetVbusDriveByBase(UINTPTR base_addr, u8 enable)
{
    u8 otg_ctrl;
    u32 otgcsr_ctrl;
    u8 ulpi_ready = 1U;

    if (PsAppUsbHost_UlpiRead(base_addr, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, &otg_ctrl) != XST_SUCCESS) {
        (void)PsAppUsbHost_RecoverUlpiPath(base_addr, "set-vbus");
        if (PsAppUsbHost_UlpiRead(base_addr, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, &otg_ctrl) != XST_SUCCESS) {
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

        if (PsAppUsbHost_UlpiWrite(base_addr, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, otg_ctrl) != XST_SUCCESS) {
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

static void PsAppUsbHost_ApplyPortKick(UINTPTR base_addr)
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

static PsAppUsbHostState *PsAppUsbHost_GetState(void)
{
    if ((g_ps_usbhost_ctx == NULL) || (g_ps_usbhost_ctx->state == NULL)) {
        return NULL;
    }
    return g_ps_usbhost_ctx->state;
}

static void PsAppUsbHost_RequestRootHubScan(void)
{
    struct usbh_bus *bus;

    bus = &g_usbhost_bus[PS_APP_USBHOST_BUS_ID];
    usbh_hub_thread_wakeup(&bus->hcd.roothub);
}

static void PsAppUsbHost_UpdateStateFromXbox(struct usbh_xbox *xbox_class)
{
    PsAppUsbHostState *state;
    const struct usb_interface_descriptor *intf_desc;

    state = PsAppUsbHost_GetState();
    if ((state == NULL) || (xbox_class == NULL) || (xbox_class->hport == NULL)) {
        return;
    }

    intf_desc = &xbox_class->hport->config.intf[xbox_class->intf].altsetting[0].intf_desc;

    state->vendor_id = xbox_class->hport->device_desc.idVendor;
    state->product_id = xbox_class->hport->device_desc.idProduct;
    state->interface_number = xbox_class->intf;
    state->ep_in_addr = (xbox_class->intin != NULL) ? xbox_class->intin->bEndpointAddress : 0U;
    state->ep_out_addr = (xbox_class->intout != NULL) ? xbox_class->intout->bEndpointAddress : 0U;
    state->interface_class = intf_desc->bInterfaceClass;
    state->interface_subclass = intf_desc->bInterfaceSubClass;
    state->interface_protocol = intf_desc->bInterfaceProtocol;
    state->port_speed = xbox_class->hport->speed;
    state->xbox_interface_active = 1U;
}

static void PsAppUsbHost_IntInResubmit(struct usbh_xbox *xbox_class);

static XStatus PsAppUsbHost_SubmitInterruptOut(struct usbh_xbox *xbox_class,
                                               const u8 *payload,
                                               u32 payload_len,
                                               u32 timeout_ms)
{
    PsAppUsbHostState *state;
    int ret;

    state = PsAppUsbHost_GetState();
    if ((state == NULL) || (xbox_class == NULL) || (payload == NULL) ||
        (payload_len == 0U) || (payload_len > sizeof(g_ps_xbox_intout_buffer)) ||
        (xbox_class->hport == NULL) || (xbox_class->intout == NULL)) {
        return XST_INVALID_PARAM;
    }

    /* 8BitDo 的 sideband 既有标准 8 字节 rumble，也有 3 字节启动包，
     * 因此这里不能把 OUT 报文长度写死成 PS_XINPUT_REPORT_SIZE_OUTPUT。 */
    memcpy(g_ps_xbox_intout_buffer, payload, payload_len);
    usbh_int_urb_fill(&xbox_class->intout_urb,
                      xbox_class->hport,
                      xbox_class->intout,
                      g_ps_xbox_intout_buffer,
                      payload_len,
                      timeout_ms,
                      NULL,
                      NULL);

    ret = usbh_submit_urb(&xbox_class->intout_urb);
    if (ret < 0) {
        state->out_report_error_count++;
        return XST_FAILURE;
    }

    state->out_report_count++;
    return XST_SUCCESS;
}

static XStatus PsAppUsbHost_Send8BitDoStartupSequence(struct usbh_xbox *xbox_class)
{
    static const u8 s_pkt0[3] = { 0x01U, 0x03U, 0x02U };
    static const u8 s_pkt1[3] = { 0x02U, 0x08U, 0x03U };
    XStatus status;

    /* 来自 8bitdo_u2w_protocol_analysis 的抓包结论：
     * Windows 在 0x84 输入流稳定出现前，会先往 0x05 发
     * 01 03 02 -> 02 08 03 -> 01 03 02。
     * 这组包若省掉，板上可能表现为“已 attach 但一直没有按键输入”。 */
    status = PsAppUsbHost_SubmitInterruptOut(xbox_class, s_pkt0, sizeof(s_pkt0), 100U);
    if (status != XST_SUCCESS) {
        return status;
    }
    status = PsAppUsbHost_SubmitInterruptOut(xbox_class, s_pkt1, sizeof(s_pkt1), 100U);
    if (status != XST_SUCCESS) {
        return status;
    }
    status = PsAppUsbHost_SubmitInterruptOut(xbox_class, s_pkt0, sizeof(s_pkt0), 100U);
    if (status != XST_SUCCESS) {
        return status;
    }

    xil_printf("[USBH] 8BitDo startup sideband sent\r\n");
    return XST_SUCCESS;
}

static void PsAppUsbHost_IntInComplete(void *arg, int nbytes)
{
    PsAppUsbHostState *state;
    struct usbh_xbox *xbox_class;

    state = PsAppUsbHost_GetState();
    xbox_class = (struct usbh_xbox *)arg;

    if ((xbox_class == NULL) || (state == NULL)) {
        return;
    }

    if (nbytes > 0) {
        state->in_report_count++;
        g_ps_usbhost_no_report_ticks = 0U;
        /* 第一帧头通常应为 00 14，用它快速判断 sideband 是否把输入流拉起来了。 */
        if (state->in_report_count == 1U) {
            xil_printf("[USBH] first input report len=%d hdr=%02x %02x\r\n",
                       nbytes,
                       (unsigned int)g_ps_xbox_intin_buffer[0],
                       (unsigned int)g_ps_xbox_intin_buffer[1]);
        }
        if ((g_ps_usbhost_ctx != NULL) && (g_ps_usbhost_ctx->input != NULL)) {
            if (PsAppInput_OnInterruptInReport(g_ps_usbhost_ctx->input,
                                               g_ps_xbox_intin_buffer,
                                               (u32)nbytes) != XST_SUCCESS) {
                state->in_report_error_count++;
            }
        }
    } else if (nbytes < 0) {
        if (nbytes != -USB_ERR_SHUTDOWN) {
            state->in_report_error_count++;
        } else {
            return;
        }
    }

    if ((xbox_class->hport == NULL) || (xbox_class->hport->connected == false)) {
        return;
    }
    if (g_ps_active_xbox != xbox_class) {
        return;
    }

    PsAppUsbHost_IntInResubmit(xbox_class);
}

static void PsAppUsbHost_IntInResubmit(struct usbh_xbox *xbox_class)
{
    PsAppUsbHostState *state;
    int ret;

    state = PsAppUsbHost_GetState();
    if ((state == NULL) || (xbox_class == NULL) ||
        (xbox_class->hport == NULL) || (xbox_class->intin == NULL)) {
        return;
    }

    usbh_int_urb_fill(&xbox_class->intin_urb,
                      xbox_class->hport,
                      xbox_class->intin,
                      g_ps_xbox_intin_buffer,
                      sizeof(g_ps_xbox_intin_buffer),
                      0U,
                      PsAppUsbHost_IntInComplete,
                      xbox_class);

    ret = usbh_submit_urb(&xbox_class->intin_urb);
    if (ret < 0) {
        state->in_report_error_count++;
        xil_printf("[USBH] intin submit failed: %d\r\n", ret);
    }
}

static void PsAppUsbHost_OnEvent(u8 busid, u8 hub_index, u8 hub_port, u8 intf, u8 event)
{
    PsAppUsbHostState *state;
    struct usbh_hubport *hport;

    (void)intf;

    state = PsAppUsbHost_GetState();
    if ((state == NULL) || (busid != PS_APP_USBHOST_BUS_ID)) {
        return;
    }

    state->event_count++;
    hport = usbh_find_hubport(busid, hub_index, hub_port);

    switch (event) {
        case USBH_EVENT_DEVICE_CONNECTED:
            state->device_present = 1U;
            state->attach_count++;
            g_ps_usbhost_recover_attempts = 0U;
            break;
        case USBH_EVENT_DEVICE_DISCONNECTED:
            state->device_present = 0U;
            state->detach_count++;
            g_ps_usbhost_recover_attempts = 0U;
            break;
        case USBH_EVENT_DEVICE_CONFIGURED:
            if (hport != NULL) {
                state->vendor_id = hport->device_desc.idVendor;
                state->product_id = hport->device_desc.idProduct;
                state->port_speed = hport->speed;
            }
            g_ps_usbhost_recover_attempts = 0U;
            break;
        default:
            break;
    }
}

static void PsAppUsbHost_IrqHandler(void *ref)
{
    struct usbh_bus *bus;
    PsAppUsbHostState *state;

    bus = (struct usbh_bus *)ref;
    state = PsAppUsbHost_GetState();

    if ((state != NULL) && (state->enabled != 0U)) {
        state->irq_count++;
    }

    if (bus != NULL) {
        USBH_IRQHandler(bus->busid);
    }
}

void usb_hc_low_level_init(struct usbh_bus *bus)
{
    PsAppUsbHostState *state;
    u32 mode;
    XStatus status;

    state = PsAppUsbHost_GetState();
    if ((bus == NULL) || (state == NULL)) {
        return;
    }

    mode = Xil_In32((UINTPTR)bus->hcd.reg_base + XUSBPS_MODE_OFFSET);
    mode &= ~XUSBPS_MODE_CM_MASK;
    mode |= XUSBPS_MODE_CM_HOST_MASK;
    Xil_Out32((UINTPTR)bus->hcd.reg_base + XUSBPS_MODE_OFFSET, mode);

    Xil_Out32((UINTPTR)bus->hcd.reg_base + XUSBPS_ISR_OFFSET, 0xFFFFFFFFU);

    if (state->irq_connected == 0U) {
#if (PS_APP_USBHOST_INTR_ID != 0U) && (PS_APP_USBHOST_INTR_PARENT != 0U)
        status = XSetupInterruptSystem((void *)bus,
                                       (void *)PsAppUsbHost_IrqHandler,
                                       PS_APP_USBHOST_INTR_ID,
                                       PS_APP_USBHOST_INTR_PARENT,
                                       XINTERRUPT_DEFAULT_PRIORITY);
        if (status == XST_SUCCESS) {
            state->irq_connected = 1U;
        } else {
            xil_printf("[USBH] interrupt setup failed: %d\r\n", status);
        }
#else
        xil_printf("[USBH] interrupt macros are unavailable in xparameters.h\r\n");
#endif
    }
}

void usb_hc_low_level2_init(struct usbh_bus *bus)
{
    u8 vid_low;
    u8 vid_high;
    u8 pid_low;
    u8 pid_high;

    if (bus == NULL) {
        return;
    }

    PsAppUsbHost_ApplyPortKick((UINTPTR)bus->hcd.reg_base);
    (void)PsAppUsbHost_SetVbusDriveByBase((UINTPTR)bus->hcd.reg_base, 1U);

    if ((PsAppUsbHost_UlpiRead((UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_LOW, &vid_low) == XST_SUCCESS) &&
        (PsAppUsbHost_UlpiRead((UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_HIGH, &vid_high) == XST_SUCCESS) &&
        (PsAppUsbHost_UlpiRead((UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_LOW, &pid_low) == XST_SUCCESS) &&
        (PsAppUsbHost_UlpiRead((UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_HIGH, &pid_high) == XST_SUCCESS)) {
        xil_printf("[USBH] ulpi phy vid=0x%02x%02x pid=0x%02x%02x\r\n",
                   (unsigned int)vid_high,
                   (unsigned int)vid_low,
                   (unsigned int)pid_high,
                   (unsigned int)pid_low);
    } else {
        xil_printf("[USBH] ulpi phy id read failed\r\n");
    }
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
    (void)bus;
}

u8 usbh_get_port_speed(struct usbh_bus *bus, const u8 port)
{
    u32 portsc;
    u32 speed;

    if ((bus == NULL) || (port == 0U)) {
        return USB_SPEED_UNKNOWN;
    }

    portsc = Xil_In32((UINTPTR)bus->hcd.reg_base + XUSBPS_PORTSCRn_OFFSET(port));
    speed = (portsc & XUSBPS_PORTSCR_PSPD_MASK) >> 26;

    if (speed == 0x02U) {
        return USB_SPEED_HIGH;
    }
    if (speed == 0x01U) {
        return USB_SPEED_LOW;
    }
    if (speed == 0x00U) {
        return USB_SPEED_FULL;
    }
    return USB_SPEED_HIGH;
}

void usbh_xbox_run(struct usbh_xbox *xbox_class)
{
    PsAppInputUsbDeviceInfo info;
    PsAppUsbHostState *state;

    state = PsAppUsbHost_GetState();
    if ((state == NULL) || (xbox_class == NULL) || (xbox_class->hport == NULL)) {
        return;
    }

    memset(&info, 0, sizeof(info));
    g_ps_active_xbox = xbox_class;
    g_ps_usbhost_no_report_ticks = 0U;

    PsAppUsbHost_UpdateStateFromXbox(xbox_class);

    info.vendor_id = xbox_class->hport->device_desc.idVendor;
    info.product_id = xbox_class->hport->device_desc.idProduct;
    info.interface_number = xbox_class->intf;
    info.interface_class = state->interface_class;
    info.interface_subclass = state->interface_subclass;
    info.interface_protocol = state->interface_protocol;
    info.ep_in_addr = state->ep_in_addr;
    info.ep_out_addr = state->ep_out_addr;
    info.ep_in_interval_ms = (xbox_class->intin != NULL) ? xbox_class->intin->bInterval : 0U;
    info.ep_out_interval_ms = (xbox_class->intout != NULL) ? xbox_class->intout->bInterval : 0U;

    if ((g_ps_usbhost_ctx != NULL) && (g_ps_usbhost_ctx->input != NULL)) {
        (void)PsAppInput_OnUsbXInputAttached(g_ps_usbhost_ctx->input, &info);
    }

    /* 仅对已知 8BitDo 接收器补启动 sideband，避免把厂商特定序列发给普通 XInput 设备。 */
    if (PsXinput_IsLikely8BitDo(info.vendor_id, info.product_id) != 0U) {
        (void)PsAppUsbHost_Send8BitDoStartupSequence(xbox_class);
    }

    PsAppUsbHost_IntInResubmit(xbox_class);
}

void usbh_xbox_stop(struct usbh_xbox *xbox_class)
{
    PsAppUsbHostState *state;

    state = PsAppUsbHost_GetState();
    if (state != NULL) {
        state->xbox_interface_active = 0U;
    }
    g_ps_usbhost_no_report_ticks = 0U;

    if (g_ps_active_xbox == xbox_class) {
        g_ps_active_xbox = NULL;
    }

    if ((g_ps_usbhost_ctx != NULL) && (g_ps_usbhost_ctx->input != NULL)) {
        PsAppInput_OnUsbDetached(g_ps_usbhost_ctx->input);
    }
}

XStatus PsAppUsbHost_Init(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostState *state;
    int ret;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->input == NULL)) {
        return XST_INVALID_PARAM;
    }

    g_ps_usbhost_ctx = ctx;
    state = ctx->state;

    if (state->enabled == 0U) {
        xil_printf("[USBH] disabled by configuration\r\n");
        return XST_SUCCESS;
    }
    if (state->initialized != 0U) {
        return XST_SUCCESS;
    }

    state->bus_id = PS_APP_USBHOST_BUS_ID;
    state->irq_count = 0U;
    state->event_count = 0U;
    state->attach_count = 0U;
    state->detach_count = 0U;
    state->in_report_count = 0U;
    state->in_report_error_count = 0U;
    state->out_report_count = 0U;
    state->out_report_error_count = 0U;
    g_ps_usbhost_no_report_ticks = 0U;

    PsAppUsbHost_ForceSlcrUsb0Config();
    PsAppUsbHost_PulsePhyReset();

    ret = usbh_initialize(PS_APP_USBHOST_BUS_ID,
                          (uintptr_t)PS_APP_USBHOST_BASE_ADDR,
                          PsAppUsbHost_OnEvent);
    if (ret < 0) {
        xil_printf("[USBH] usbh_initialize failed: %d\r\n", ret);
        return XST_FAILURE;
    }

    state->initialized = 1U;
    PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
    (void)PsAppUsbHost_SetVbusDriveByBase((UINTPTR)PS_APP_USBHOST_BASE_ADDR, 1U);
    g_ps_usbhost_last_portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    g_ps_usbhost_force_scan_pending = 1U;
    g_ps_usbhost_recover_cooldown = 0U;
    g_ps_usbhost_recover_attempts = 0U;
    xil_printf("[USBH] init ok: bus=%u base=0x%08x\r\n",
               (unsigned int)state->bus_id,
               (unsigned int)PS_APP_USBHOST_BASE_ADDR);
    return XST_SUCCESS;
}

void PsAppUsbHost_Service(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostState *state;
    u32 portsc;
    u32 changed_bits;

    if ((ctx != NULL) && (g_ps_usbhost_ctx == NULL)) {
        g_ps_usbhost_ctx = ctx;
    }

    state = PsAppUsbHost_GetState();
    if ((state == NULL) || (state->enabled == 0U) || (state->initialized == 0U)) {
        return;
    }

    if (g_ps_usbhost_force_scan_pending != 0U) {
        PsAppUsbHost_RequestRootHubScan();
        g_ps_usbhost_force_scan_pending = 0U;
    }

    if (g_ps_usbhost_recover_cooldown > 0U) {
        g_ps_usbhost_recover_cooldown--;
    }

    portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    if ((portsc & XUSBPS_PORTSCR_PHCD_MASK) != 0U) {
        PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
        (void)PsAppUsbHost_SetVbusDriveByBase((UINTPTR)PS_APP_USBHOST_BASE_ADDR, 1U);
        portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    }
    changed_bits = (portsc ^ g_ps_usbhost_last_portsc) &
                   (XUSBPS_PORTSCR_CSC_MASK | XUSBPS_PORTSCR_CCS_MASK);
    if (changed_bits != 0U) {
        PsAppUsbHost_RequestRootHubScan();
    }

    if (((portsc & XUSBPS_PORTSCR_CCS_MASK) != 0U) &&
        (state->device_present == 0U) &&
        (state->xbox_interface_active == 0U) &&
        (g_ps_usbhost_recover_cooldown == 0U) &&
        (g_ps_usbhost_recover_attempts < PS_APP_USBHOST_ENUM_MAX_RETRY)) {
        XStatus rst_status;

        rst_status = PsAppUsbHost_PortReset(g_ps_usbhost_ctx);
        if (rst_status != XST_SUCCESS) {
            PsAppUsbHost_RequestRootHubScan();
        }
        g_ps_usbhost_recover_attempts++;
        g_ps_usbhost_recover_cooldown = PS_APP_USBHOST_ENUM_RETRY_COOLDOWN;
        xil_printf("[USBH] enumerate recovery retry=%u status=%d\r\n",
                   (unsigned int)g_ps_usbhost_recover_attempts,
                   (int)rst_status);
    }

    if ((state->xbox_interface_active != 0U) &&
        (g_ps_active_xbox != NULL) &&
        (PsXinput_IsLikely8BitDo(state->vendor_id, state->product_id) != 0U) &&
        (state->in_report_count == 0U)) {
        g_ps_usbhost_no_report_ticks++;
        /* 协议分析建议：若 attach 后长时间没有任何 20B 输入包，可补打一轮
         * 3 字节 startup sideband 做轻量恢复，先不要急着整口复位。 */
        if (g_ps_usbhost_no_report_ticks >= PS_APP_USBHOST_8BITDO_REPORT_TIMEOUT_TICKS) {
            if (PsAppUsbHost_Send8BitDoStartupSequence(g_ps_active_xbox) == XST_SUCCESS) {
                xil_printf("[USBH] 8BitDo sideband retry after no input\r\n");
            }
            g_ps_usbhost_no_report_ticks = 0U;
        }
    }

    g_ps_usbhost_last_portsc = portsc;
}

static void PsAppUsbHost_PrintSlcrSummary(void)
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

void PsAppUsbHost_PrintStatus(const PsAppUsbHostContext *ctx)
{
    const PsAppUsbHostState *state;
    u32 cmd;
    u32 mode;
    u32 isr;
    u32 ier;
    u32 portsc;
    u32 hcsparams;
    u32 otgcsr;
    u32 ulpiview;
    u32 line_status;
    u32 port_speed;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        xil_printf("[USBH] status unavailable\r\n");
        return;
    }

    state = ctx->state;
    xil_printf("[USBH] en=%u init=%u irq=%u dev=%u xbox=%u bus=%u speed=%u\r\n",
               (unsigned int)state->enabled,
               (unsigned int)state->initialized,
               (unsigned int)state->irq_connected,
               (unsigned int)state->device_present,
               (unsigned int)state->xbox_interface_active,
               (unsigned int)state->bus_id,
               (unsigned int)state->port_speed);
    xil_printf("[USBH] vid=0x%04x pid=0x%04x intf=%u cls=%02x/%02x/%02x ep_in=0x%02x ep_out=0x%02x\r\n",
               (unsigned int)state->vendor_id,
               (unsigned int)state->product_id,
               (unsigned int)state->interface_number,
               (unsigned int)state->interface_class,
               (unsigned int)state->interface_subclass,
               (unsigned int)state->interface_protocol,
               (unsigned int)state->ep_in_addr,
               (unsigned int)state->ep_out_addr);
    xil_printf("[USBH] irq_cnt=%u events=%u attach=%u detach=%u in_ok=%u in_err=%u out_ok=%u out_err=%u\r\n",
               (unsigned int)state->irq_count,
               (unsigned int)state->event_count,
               (unsigned int)state->attach_count,
               (unsigned int)state->detach_count,
               (unsigned int)state->in_report_count,
               (unsigned int)state->in_report_error_count,
               (unsigned int)state->out_report_count,
               (unsigned int)state->out_report_error_count);

    if (state->initialized != 0U) {
        PsAppUsbHost_PrintSlcrSummary();

        cmd = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_CMD_OFFSET);
        mode = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_MODE_OFFSET);
        isr = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_ISR_OFFSET);
        ier = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_IER_OFFSET);
        portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
        hcsparams = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_HCSPARAMS_OFFSET);
        otgcsr = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_OTGCSR_OFFSET);
        ulpiview = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_ULPIVIEW_OFFSET);
        line_status = (portsc & XUSBPS_PORTSCR_LS_MASK) >> 10;
        port_speed = (portsc & XUSBPS_PORTSCR_PSPD_MASK) >> 26;

        xil_printf("[USBH] reg cmd=0x%08x mode=0x%08x isr=0x%08x ier=0x%08x hcs=0x%08x\r\n",
                   (unsigned int)cmd,
                   (unsigned int)mode,
                   (unsigned int)isr,
                   (unsigned int)ier,
                   (unsigned int)hcsparams);
        xil_printf("[USBH] reg portsc=0x%08x otg=0x%08x ulpi=0x%08x\r\n",
                   (unsigned int)portsc,
                   (unsigned int)otgcsr,
                   (unsigned int)ulpiview);
        xil_printf("[USBH] port1 ccs=%u csc=%u pe=%u pec=%u oca=%u occ=%u susp=%u pr=%u pp=%u owner=%u phcd=%u ls=%u pspd=%u\r\n",
                   (unsigned int)((portsc & XUSBPS_PORTSCR_CCS_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_CSC_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PE_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PEC_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_OCA_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_OCC_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_SUSP_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PR_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PP_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PO_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PHCD_MASK) != 0U),
                   (unsigned int)line_status,
                   (unsigned int)port_speed);
        xil_printf("[USBH] intr ui=%u ue=%u pc=%u aa=%u hch=%u ps=%u as=%u ulpi=%u\r\n",
                   (unsigned int)((isr & XUSBPS_IXR_UI_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_UE_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_PC_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_AA_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_HCH_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_PS_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_AS_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_ULPI_MASK) != 0U));
    }
}

void PsAppUsbHost_ForceRootHubScan(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostState *state;

    if ((ctx != NULL) && (g_ps_usbhost_ctx == NULL)) {
        g_ps_usbhost_ctx = ctx;
    }

    state = PsAppUsbHost_GetState();
    if ((state == NULL) || (state->enabled == 0U) || (state->initialized == 0U)) {
        return;
    }

    g_ps_usbhost_force_scan_pending = 1U;
    PsAppUsbHost_RequestRootHubScan();
}

XStatus PsAppUsbHost_SetVbusDrive(PsAppUsbHostContext *ctx, u8 enable)
{
    PsAppUsbHostState *state;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U)) {
        return XST_FAILURE;
    }

    if (enable != 0U) {
        PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
    }

    if (PsAppUsbHost_SetVbusDriveByBase((UINTPTR)PS_APP_USBHOST_BASE_ADDR, enable) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    g_ps_usbhost_last_portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    PsAppUsbHost_ForceRootHubScan(ctx);
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_DumpUlpi(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostState *state;
    u8 vid_low;
    u8 vid_high;
    u8 pid_low;
    u8 pid_high;
    u8 otg_ctrl;
    u8 func_ctrl;
    u8 iface_ctrl;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U)) {
        return XST_FAILURE;
    }

    if ((PsAppUsbHost_UlpiRead((UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_LOW, &vid_low) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead((UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_HIGH, &vid_high) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead((UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_LOW, &pid_low) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead((UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_HIGH, &pid_high) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead((UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, &otg_ctrl) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead((UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_FUNC_CTRL, &func_ctrl) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead((UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_IFACE_CTRL, &iface_ctrl) != XST_SUCCESS)) {
        xil_printf("[USBH] ulpi dump failed (viewport timeout)\r\n");
        return XST_FAILURE;
    }

    xil_printf("[USBH] ulpi phy vid=0x%02x%02x pid=0x%02x%02x otg=0x%02x func=0x%02x iface=0x%02x\r\n",
               (unsigned int)vid_high,
               (unsigned int)vid_low,
               (unsigned int)pid_high,
               (unsigned int)pid_low,
               (unsigned int)otg_ctrl,
               (unsigned int)func_ctrl,
               (unsigned int)iface_ctrl);
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_PortReset(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostState *state;
    u32 portsc;
    u32 timeout_ms;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U)) {
        return XST_FAILURE;
    }

    PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
    (void)PsAppUsbHost_SetVbusDriveByBase((UINTPTR)PS_APP_USBHOST_BASE_ADDR, 1U);

    portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    portsc &= ~XUSBPS_PORTSCR_PE_MASK;
    portsc |= (XUSBPS_PORTSCR_PR_MASK | XUSBPS_PORTSCR_PP_MASK | PS_APP_USBHOST_PORTSC_CHANGE_BITS);
    Xil_Out32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET, portsc);

    usb_osal_msleep(55U);

    portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    portsc &= ~XUSBPS_PORTSCR_PR_MASK;
    portsc |= (XUSBPS_PORTSCR_PP_MASK | PS_APP_USBHOST_PORTSC_CHANGE_BITS);
    Xil_Out32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET, portsc);

    timeout_ms = 0U;
    while (timeout_ms < PS_APP_USBHOST_PORT_RESET_TIMEOUT_MS) {
        portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
        if ((portsc & XUSBPS_PORTSCR_PR_MASK) == 0U) {
            break;
        }
        usb_osal_msleep(1U);
        timeout_ms++;
    }

    g_ps_usbhost_last_portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    PsAppUsbHost_ForceRootHubScan(ctx);

    if (timeout_ms >= PS_APP_USBHOST_PORT_RESET_TIMEOUT_MS) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_SetRumble(PsAppUsbHostContext *ctx, u8 large_motor, u8 small_motor)
{
    PsAppUsbHostState *state;
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->input == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U) ||
        (state->xbox_interface_active == 0U) ||
        (g_ps_active_xbox == NULL) ||
        (g_ps_active_xbox->hport == NULL) ||
        (g_ps_active_xbox->intout == NULL)) {
        return XST_FAILURE;
    }

    PsAppInput_BuildRumbleReport(large_motor, small_motor, g_ps_xbox_intout_buffer);
    status = PsAppUsbHost_SubmitInterruptOut(g_ps_active_xbox,
                                             g_ps_xbox_intout_buffer,
                                             sizeof(g_ps_xbox_intout_buffer),
                                             100U);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    (void)state;
    return XST_SUCCESS;
}
