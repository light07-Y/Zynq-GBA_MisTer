#ifndef PS_APP_USBHOST_IMPL_INTERNAL_H
#define PS_APP_USBHOST_IMPL_INTERNAL_H

#include "UsbHost/Inc/ps_app_usbhost.h"

#include "usbh_core.h"
#include "usbh_xbox.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xusbps_hw.h"

#ifdef __cplusplus
extern "C" {
#endif

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
#define PS_APP_USBHOST_ENUM_MAX_RETRY 5U
#define PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_BASE PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_BASE_TICKS
#define PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_MAX PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_MAX_TICKS
#define PS_APP_USBHOST_PHY_RESET_ASSERT_MS 5U
#define PS_APP_USBHOST_PHY_RESET_RELEASE_MS 20U
#define PS_APP_USBHOST_8BITDO_REPORT_TIMEOUT_TICKS 1000U
/*
 * 通用“首包等待超时”：
 * attach 之后如果一直没有任何 IN 报告，Service() 会在该超时后触发一次恢复动作
 * （例如 8BitDo sideband 兜底），用于处理“设备枚举成功但输入链路未真正拉起”。
 */
/*
 * 0x310B 首次兜底采用更短等待，减少“已连上但首包出现较晚”的体感延迟。
 * 该值仅用于“每次 attach 的第一次兜底尝试”，后续仍走通用较长超时，避免抖动。
 */
#define PS_APP_USBHOST_8BITDO_310B_FIRST_FALLBACK_TICKS 300U
/*
 * INTERFACE_STOP 到 DEVICE_DISCONNECTED 之间存在事件重排/抖动窗口。
 * 这里给“逻辑断连”留一个确认窗口，避免刚连上就被误判为 detached。
 */
#define PS_APP_USBHOST_DETACH_DEFER_TICKS 500U

#define PS_APP_USBHOST_ULPI_ADDR_SHIFT 16U
#define PS_APP_USBHOST_ULPI_DATRD_SHIFT 8U

#define PS_APP_USBHOST_ULPI_REG_VENDOR_ID_LOW 0x00U
#define PS_APP_USBHOST_ULPI_REG_VENDOR_ID_HIGH 0x01U
#define PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_LOW 0x02U
#define PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_HIGH 0x03U
#define PS_APP_USBHOST_ULPI_REG_OTG_CTRL 0x0AU
#define PS_APP_USBHOST_ULPI_REG_FUNC_CTRL 0x04U
#define PS_APP_USBHOST_ULPI_REG_IFACE_CTRL 0x07U

#define PS_APP_USBHOST_ULPI_OTG_DISCHRGVBUS (1U << 3)
#define PS_APP_USBHOST_ULPI_OTG_CHRGVBUS (1U << 4)
#define PS_APP_USBHOST_ULPI_OTG_DRVVBUS (1U << 5)
#define PS_APP_USBHOST_ULPI_OTG_DRVVBUS_EXT (1U << 6)

/*
 * 只主动确认 PEC/OCC，不主动确认 CSC。
 *
 * 背景：
 * CSC(C-Connection) 由 Hub 线程消费后再清除；如果我们在“普通维护写 PORTSC”时提前清 CSC，
 * 会把“上电前已插入接收器”等场景的连接变化事件抹掉，导致后续根集线器看不到连接沿。
 */
#define PS_APP_USBHOST_PORTSC_CHANGE_BITS \
    (XUSBPS_PORTSCR_PEC_MASK | XUSBPS_PORTSCR_OCC_MASK)

#define PS_APP_USBHOST_OTGSC_CTRL_BITS \
    (XUSBPS_OTGSC_VD_MASK | XUSBPS_OTGSC_VC_MASK | XUSBPS_OTGSC_HAAR_MASK | \
     XUSBPS_OTGSC_OT_MASK | XUSBPS_OTGSC_DP_MASK | XUSBPS_OTGSC_IDPU_MASK | \
     XUSBPS_OTGSC_HADP_MASK | XUSBPS_OTGSC_HABA_MASK)

#define PS_APP_USBHOST_SLCR_LOCK_ADDR 0xF8000004U
#define PS_APP_USBHOST_SLCR_UNLOCK_ADDR 0xF8000008U
#define PS_APP_USBHOST_SLCR_LOCK_CODE 0x0000767BU
#define PS_APP_USBHOST_SLCR_UNLOCK_CODE 0x0000DF0DU

#define PS_APP_USBHOST_SLCR_APER_CLK_CTRL_ADDR 0xF800012CU
#define PS_APP_USBHOST_SLCR_USB0_CLK_CTRL_ADDR 0xF8000130U
#define PS_APP_USBHOST_SLCR_USB_RST_CTRL_ADDR 0xF8000210U

#define PS_APP_USBHOST_APER_USB0_CPU_1XCLKACT_MASK (1U << 2U)
#define PS_APP_USBHOST_APER_GPIO_CPU_1XCLKACT_MASK (1U << 22U)
#define PS_APP_USBHOST_USB0_CLKACT_MASK 0x00000001U
#define PS_APP_USBHOST_USB_RST_CTRL_MASK 0x00000003U

#define PS_APP_USBHOST_MIO28_CFG_ADDR 0xF8000770U
#define PS_APP_USBHOST_MIO46_CFG_ADDR 0xF80007B8U
#define PS_APP_USBHOST_MIO47_CFG_ADDR 0xF80007BCU

#if defined(XPAR_XGPIOPS_0_BASEADDR)
#define PS_APP_USBHOST_GPIO_BASE_ADDR ((UINTPTR)XPAR_XGPIOPS_0_BASEADDR)
#define PS_APP_USBHOST_GPIO_DIRM_1_OFFSET 0x00000244U
#define PS_APP_USBHOST_GPIO_OEN_1_OFFSET 0x00000248U
#define PS_APP_USBHOST_GPIO_MASK_1_LSW_OFF 0x00000008U
#define PS_APP_USBHOST_PHY_RESET_BIT_BANK1 (1U << 14U)
#endif

typedef struct PsAppUsbHostImplContext {
    PsAppUsbHostContext *public_context_ptr;
    /* 当前被我们认定为“有效输入链路”的 xbox class 实例。用于过滤旧实例晚到事件。 */
    struct usbh_xbox *active_xbox_ptr;
    u8 *int_in_report_buffer_ptr;
    u8 *int_out_report_buffer_ptr;
    u32 int_in_report_buffer_size;
    u32 int_out_report_buffer_size;
    u32 last_portsc;
    u8 force_scan_pending;
    u32 recover_cooldown_ticks;
    u8 recover_attempts;
    u8 ulpi_recovering;
    /* 自本次 attach 以来持续“无任何 IN 报告”的计数。由 IN 回调命中后清零。 */
    u32 no_report_ticks;
    u32 startup_sideband_log_count;
    u32 retry_sideband_log_count;
    u8 phy_connected;
    u8 phy_raw_ccs;
    u32 phy_stable_ticks;
    u32 enum_observe_ticks;
    u32 stale_detach_ticks;
    /*
     * 首包等待标记：run() 置 1，收到首包后清 0。
     * 仅在该标记为 1 且 no_report_ticks 超时时，才允许触发“无首包兜底”逻辑。
     */
    u8 waiting_first_input_after_attach;
    /*
     * 断连延迟裁决窗口：
     * 某些固件/时序下，INTERFACE_STOP 与 DEVICE_DISCONNECTED 可能短暂抖动或重排，
     * 这里先不立即通知输入层 detached，而是延迟若干 tick 再确认。
     */
    u8 detach_defer_active;
    u32 detach_defer_ticks;
    /* 每次 attach 最多执行一次“首包兜底 sideband”，避免重复触发重枚举。 */
    u8 sideband_attempted_this_attach;
} PsAppUsbHostImplContext;

PsAppUsbHostImplContext *PsAppUsbHost_ImplGetContext(void);
void PsAppUsbHost_ImplSetContext(PsAppUsbHostImplContext *impl_ctx);

PsAppUsbHostState *PsAppUsbHost_GetState(PsAppUsbHostImplContext *impl_ctx);
void PsAppUsbHost_ResetDeviceIdentity(PsAppUsbHostState *state);
u32 PsAppUsbHost_GetRecoverCooldownTicks(u8 attempt_no);
void PsAppUsbHost_ForceLogicalDetach(PsAppUsbHostImplContext *impl_ctx, const char *reason);
void PsAppUsbHost_RequestRootHubScan(void);
XStatus PsAppUsbHost_VbusBounceRecover(PsAppUsbHostImplContext *impl_ctx);
void PsAppUsbHost_UpdateStateFromXbox(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class);
void PsAppUsbHost_OnEvent(u8 busid, u8 hub_index, u8 hub_port, u8 intf, u8 event);
void PsAppUsbHost_LowLevelInit(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus);
void PsAppUsbHost_LowLevelPostInit(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus);
void PsAppUsbHost_LowLevelDeinit(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus);
u8 PsAppUsbHost_GetPortSpeed(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus, u8 port);

void PsAppUsbHost_ForceSlcrUsb0Config(void);
void PsAppUsbHost_PulsePhyReset(void);
void PsAppUsbHost_ApplyPortKick(UINTPTR base_addr);
void PsAppUsbHost_PrintSlcrSummary(void);
XStatus PsAppUsbHost_UlpiRead(PsAppUsbHostImplContext *impl_ctx, UINTPTR base_addr, u8 reg_addr, u8 *value_out);
XStatus PsAppUsbHost_SetVbusDriveByBase(PsAppUsbHostImplContext *impl_ctx, UINTPTR base_addr, u8 enable);

XStatus PsAppUsbHost_SubmitInterruptOut(PsAppUsbHostImplContext *impl_ctx,
                                        struct usbh_xbox *xbox_class,
                                        const u8 *payload,
                                        u32 payload_len,
                                        u32 timeout_ms);
XStatus PsAppUsbHost_Send8BitDoStartupSequence(PsAppUsbHostImplContext *impl_ctx,
                                               struct usbh_xbox *xbox_class);
void PsAppUsbHost_OnXboxRun(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class);
void PsAppUsbHost_OnXboxStop(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class);

#ifdef __cplusplus
}
#endif

#endif
