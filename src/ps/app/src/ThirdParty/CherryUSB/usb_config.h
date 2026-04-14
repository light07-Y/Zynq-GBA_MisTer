#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

#include "xil_printf.h"

/* ================ USB common Configuration ================ */

#ifndef CONFIG_USB_PRINTF
#define CONFIG_USB_PRINTF xil_printf
#endif

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_WARNING
#endif

/*
 * Zynq PS USB Host 使用 Cache + 显式 flush/invalidate 方案，
 * 因此这里不依赖专门的 non-cache section。
 */
#define CONFIG_USB_DCACHE_ENABLE
#define CONFIG_USB_ALIGN_SIZE 32
#define USB_NOCACHE_RAM_SECTION

/* ================ USB HOST Stack Configuration ================ */

#define CONFIG_USBHOST_MAX_BUS               1
#define CONFIG_USBHOST_MAX_RHPORTS           1
#define CONFIG_USBHOST_MAX_EXTHUBS           0
#define CONFIG_USBHOST_MAX_EHPORTS           1
#define CONFIG_USBHOST_MAX_INTERFACES        4
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS  2
#define CONFIG_USBHOST_MAX_ENDPOINTS         4
#define CONFIG_USBHOST_MAX_XBOX_CLASS        1
#define CONFIG_USBHOST_DEV_NAMELEN           16
/*
 * 对未收录 PID 的 XInput 设备启用接口特征兜底匹配:
 * class=0xFF, subclass=0x5D, protocol=0x01
 */
#define CONFIG_USBHOST_XBOX_GENERIC_MATCH    1

#ifndef CONFIG_USBHOST_PSC_PRIO
#define CONFIG_USBHOST_PSC_PRIO 2
#endif

#ifndef CONFIG_USBHOST_PSC_STACKSIZE
#define CONFIG_USBHOST_PSC_STACKSIZE 3072
#endif

#ifndef CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN 512
#endif

#ifndef CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 500
#endif

/* ================ USB Host Port Configuration ================ */

/*
 * Zynq-7000 PS USB 控制器的 EHCI capability block 在 base + 0x100。
 */
#define CONFIG_USB_EHCI_HCCR_OFFSET     0x100
#define CONFIG_USB_EHCI_FRAME_LIST_SIZE 1024
#define CONFIG_USB_EHCI_QH_NUM          12
#define CONFIG_USB_EHCI_QTD_NUM         (CONFIG_USB_EHCI_QH_NUM * 3)
#define CONFIG_USB_EHCI_DESC_DCACHE_ENABLE

#ifndef usb_phyaddr2ramaddr
#define usb_phyaddr2ramaddr(addr) (addr)
#endif

#ifndef usb_ramaddr2phyaddr
#define usb_ramaddr2phyaddr(addr) (addr)
#endif

#endif
