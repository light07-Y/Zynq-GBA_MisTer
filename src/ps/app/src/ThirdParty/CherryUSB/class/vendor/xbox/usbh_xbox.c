/*
 * Copyright (c) 2024 Till Harbaum
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbh_core.h"
#include "usbh_xbox.h"

#define DEV_FORMAT "/dev/xbox%d"

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t g_xbox_buf[128];

static struct usbh_xbox g_xbox_class[CONFIG_USBHOST_MAX_XBOX_CLASS];
static uint32_t g_devinuse = 0;

static struct usbh_xbox *usbh_xbox_class_alloc(void)
{
    uint8_t devno;

    for (devno = 0; devno < CONFIG_USBHOST_MAX_XBOX_CLASS; devno++) {
        if ((g_devinuse & (1U << devno)) == 0) {
            g_devinuse |= (1U << devno);
            memset(&g_xbox_class[devno], 0, sizeof(struct usbh_xbox));
            g_xbox_class[devno].minor = devno;
            return &g_xbox_class[devno];
        }
    }
    return NULL;
}

static void usbh_xbox_class_free(struct usbh_xbox *xbox_class)
{
    uint8_t devno = xbox_class->minor;

    if (devno < 32) {
        g_devinuse &= ~(1U << devno);
    }
    memset(xbox_class, 0, sizeof(struct usbh_xbox));
}

int usbh_xbox_connect(struct usbh_hubport *hport, uint8_t intf)
{
    struct usb_endpoint_descriptor *ep_desc;
    const struct usb_interface_descriptor *intf_desc;
    uint8_t int_in_found = 0U;

    struct usbh_xbox *xbox_class = usbh_xbox_class_alloc();
    if (xbox_class == NULL) {
        USB_LOG_ERR("Fail to alloc xbox_class\r\n");
        return -USB_ERR_NOMEM;
    }

    xbox_class->hport = hport;
    xbox_class->intf = intf;

    hport->config.intf[intf].priv = xbox_class;
    intf_desc = &hport->config.intf[intf].altsetting[0].intf_desc;

    for (uint8_t i = 0; i < hport->config.intf[intf].altsetting[0].intf_desc.bNumEndpoints; i++) {
        ep_desc = &hport->config.intf[intf].altsetting[0].ep[i].ep_desc;
        if (USB_GET_ENDPOINT_TYPE(ep_desc->bmAttributes) != USB_ENDPOINT_TYPE_INTERRUPT) {
            continue;
        }

        if (ep_desc->bEndpointAddress & 0x80) {
            USBH_EP_INIT(xbox_class->intin, ep_desc);
            int_in_found = 1U;
        } else {
            USBH_EP_INIT(xbox_class->intout, ep_desc);
        }
    }

    if (int_in_found == 0U) {
        USB_LOG_ERR("XBOX class requires at least one interrupt IN endpoint\r\n");
        usbh_xbox_class_free(xbox_class);
        hport->config.intf[intf].priv = NULL;
        return -USB_ERR_INVAL;
    }

    snprintf(hport->config.intf[intf].devname, CONFIG_USBHOST_DEV_NAMELEN, DEV_FORMAT, xbox_class->minor);

    if ((intf_desc->bInterfaceClass != USB_DEVICE_CLASS_VEND_SPECIFIC) ||
        (intf_desc->bInterfaceSubClass != 0x5d) ||
        (intf_desc->bInterfaceProtocol != 0x01)) {
        USB_LOG_WRN("XBOX relaxed bind VID:0x%04x PID:0x%04x intf:%u cls:%02x/%02x/%02x\r\n",
                    hport->device_desc.idVendor,
                    hport->device_desc.idProduct,
                    intf,
                    intf_desc->bInterfaceClass,
                    intf_desc->bInterfaceSubClass,
                    intf_desc->bInterfaceProtocol);
    }

    USB_LOG_INFO("Register XBOX Class:%s\r\n", hport->config.intf[intf].devname);

    usbh_xbox_run(xbox_class);
    return 0;
}

int usbh_xbox_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    int ret = 0;

    struct usbh_xbox *xbox_class = (struct usbh_xbox *)hport->config.intf[intf].priv;

    if (xbox_class) {
        if (xbox_class->intin) {
            usbh_kill_urb(&xbox_class->intin_urb);
        }

        if (xbox_class->intout) {
            usbh_kill_urb(&xbox_class->intout_urb);
        }

        if (hport->config.intf[intf].devname[0] != '\0') {
            usb_osal_thread_schedule_other();
            USB_LOG_INFO("Unregister XBOX Class:%s\r\n", hport->config.intf[intf].devname);
            /*
             * 注意：底层一旦走到 disconnect，这里会立即回调 stop。
             * 上层（ps_app_usbhost_report_channel / enum_recovery）需要自行做
             * “延迟判定 detached”防抖，避免瞬态重枚举造成 UI 抖动。
             */
            usbh_xbox_stop(xbox_class);
        }

        usbh_xbox_class_free(xbox_class);
    }

    return ret;
}

__WEAK void usbh_xbox_run(struct usbh_xbox *xbox_class)
{
    (void)xbox_class;
}

__WEAK void usbh_xbox_stop(struct usbh_xbox *xbox_class)
{
    (void)xbox_class;
}

const struct usbh_class_driver xbox_class_driver = {
    .driver_name = "xbox",
    .connect = usbh_xbox_connect,
    .disconnect = usbh_xbox_disconnect
};

static const uint16_t xbox_id_table[][2] = {
    { 0x0079, 0x18d4 }, // GPD Win 2 X-Box Controller
    { 0x03eb, 0xff01 }, // Wooting One (Legacy)
    { 0x03eb, 0xff02 }, // Wooting Two (Legacy)
    { 0x044f, 0xb326 }, // Thrustmaster Gamepad GP XID
    { 0x045e, 0x028e }, // Microsoft X-Box 360 pad
    { 0x045e, 0x028f }, // Microsoft X-Box 360 pad v2
    { 0x046d, 0xc21d }, // Logitech Gamepad F310
    { 0x046d, 0xc21e }, // Logitech Gamepad F510
    { 0x046d, 0xc21f }, // Logitech Gamepad F710
    { 0x046d, 0xc242 }, // Logitech Chillstream Controller
    { 0x046d, 0xcaa3 }, // Logitech DriveFx Racing Wheel
    { 0x056e, 0x2004 }, // Elecom JC-U3613M
    { 0x06a3, 0xf51a }, // Saitek P3600
    { 0x0738, 0x4716 }, // Mad Catz Wired Xbox 360 Controller
    { 0x0738, 0x4718 }, // Mad Catz Street Fighter IV FightStick SE
    { 0x0738, 0x4726 }, // Mad Catz Xbox 360 Controller
    { 0x0738, 0x4736 }, // Mad Catz MicroCon Gamepad
    { 0x0738, 0x4740 }, // Mad Catz Beat Pad
    { 0x0738, 0x9871 }, // Mad Catz Portable Drum
    { 0x0738, 0xb726 }, // Mad Catz Xbox controller - MW2
    { 0x0738, 0xbeef }, // Mad Catz JOYTECH NEO SE Advanced GamePad
    { 0x0738, 0xcb02 }, // Saitek Cyborg Rumble Pad - PC/Xbox 360
    { 0x0738, 0xcb03 }, // Saitek P3200 Rumble Pad - PC/Xbox 360
    { 0x0738, 0xcb29 }, // Saitek Aviator Stick AV8R02
    { 0x0738, 0xf738 }, // Super SFIV FightStick TE S
    { 0x07ff, 0xffff }, // Mad Catz GamePad
    { 0x0e6f, 0x0113 }, // Afterglow AX.1 Gamepad for Xbox 360
    { 0x0e6f, 0x011f }, // Rock Candy Gamepad Wired Controller
    { 0x0e6f, 0x0131 }, // PDP EA Sports Controller
    { 0x0e6f, 0x0133 }, // Xbox 360 Wired Controller
    { 0x0e6f, 0x0201 }, // Pelican PL-3601 'TSZ' Wired Xbox 360 Controller
    { 0x0e6f, 0x0213 }, // Afterglow Gamepad for Xbox 360
    { 0x0e6f, 0x021f }, // Rock Candy Gamepad for Xbox 360
    { 0x0e6f, 0x0301 }, // Logic3 Controller
    { 0x0e6f, 0x0401 }, // Logic3 Controller
    { 0x0e6f, 0x0413 }, // Afterglow AX.1 Gamepad for Xbox 360
    { 0x0e6f, 0x0501 }, // PDP Xbox 360 Controller
    { 0x0e6f, 0xf900 }, // PDP Afterglow AX.1
    { 0x0f0d, 0x000a }, // Hori Co. DOA4 FightStick
    { 0x0f0d, 0x000c }, // Hori PadEX Turbo
    { 0x1038, 0x1430 }, // SteelSeries Stratus Duo
    { 0x1038, 0x1431 }, // SteelSeries Stratus Duo
    { 0x11c9, 0x55f0 }, // Nacon GC-100XF
    { 0x1209, 0x2882 }, // Ardwiino Controller
    { 0x12ab, 0x0301 }, // PDP AFTERGLOW AX.1
    { 0x1430, 0x4748 }, // RedOctane Guitar Hero X-plorer
    { 0x1430, 0xf801 }, // RedOctane Controller
    { 0x146b, 0x0601 }, // BigBen Interactive XBOX 360 Controller
    { 0x1532, 0x0037 }, // Razer Sabertooth
    { 0x15e4, 0x3f00 }, // Power A Mini Pro Elite
    { 0x15e4, 0x3f0a }, // Xbox Airflo wired controller
    { 0x15e4, 0x3f10 }, // Batarang Xbox 360 controller
    { 0x162e, 0xbeef }, // Joytech Neo-Se Take2
    { 0x1689, 0xfd00 }, // Razer Onza Tournament Edition
    { 0x1689, 0xfd01 }, // Razer Onza Classic Edition
    { 0x1689, 0xfe00 }, // Razer Sabertooth
    { 0x1949, 0x041a }, // Amazon Game Controller
    { 0x1bad, 0x0002 }, // Harmonix Rock Band Guitar
    { 0x1bad, 0xf016 }, // Mad Catz Xbox 360 Controller
    { 0x1bad, 0xf021 }, // Mad Cats Ghost Recon FS GamePad
    { 0x1bad, 0xf023 }, // MLG Pro Circuit Controller (Xbox)
    { 0x1bad, 0xf025 }, // Mad Catz Call Of Duty
    { 0x1bad, 0xf027 }, // Mad Catz FPS Pro
    { 0x1bad, 0xf028 }, // Street Fighter IV FightPad
    { 0x1bad, 0xf030 }, // Mad Catz Xbox 360 MC2 MicroCon Racing Wheel
    { 0x1bad, 0xf036 }, // Mad Catz MicroCon GamePad Pro
    { 0x1bad, 0xf038 }, // Street Fighter IV FightStick TE
    { 0x1bad, 0xf501 }, // HoriPad EX2 Turbo
    { 0x1bad, 0xf506 }, // Hori Real Arcade Pro.EX Premium VLX
    { 0x1bad, 0xf900 }, // Harmonix Xbox 360 Controller
    { 0x1bad, 0xf901 }, // Gamestop Xbox 360 Controller
    { 0x1bad, 0xf903 }, // Tron Xbox 360 controller
    { 0x1bad, 0xf904 }, // PDP Versus Fighting Pad
    { 0x1bad, 0xfa01 }, // MadCatz GamePad
    { 0x1bad, 0xfd00 }, // Razer Onza TE
    { 0x1bad, 0xfd01 }, // Razer Onza
    { 0x20d6, 0x2001 }, // BDA Xbox Series X Wired Controller
    { 0x20d6, 0x281f }, // PowerA Wired Controller For Xbox 360
    { 0x24c6, 0x5300 }, // PowerA MINI PROEX Controller
    { 0x24c6, 0x5303 }, // Xbox Airflo wired controller
    { 0x24c6, 0x530a }, // Xbox 360 Pro EX Controller
    { 0x24c6, 0x531a }, // PowerA Pro Ex
    { 0x24c6, 0x5397 }, // FUS1ON Tournament Controller
    { 0x24c6, 0x5500 }, // Hori XBOX 360 EX 2 with Turbo
    { 0x24c6, 0x5501 }, // Hori Real Arcade Pro VX-SA
    { 0x24c6, 0x5506 }, // Hori SOULCALIBUR V Stick
    { 0x24c6, 0x550d }, // Hori GEM Xbox controller
    { 0x24c6, 0x5b00 }, // ThrustMaster Ferrari 458 Racing Wheel
    { 0x24c6, 0x5b02 }, // Thrustmaster, Inc. GPX Controller
    { 0x24c6, 0x5b03 }, // Thrustmaster Ferrari 458 Racing Wheel
    { 0x24c6, 0x5d04 }, // Razer Sabertooth
    { 0x24c6, 0xfafe }, // Rock Candy Gamepad for Xbox 360
    { 0x2563, 0x058d }, // OneXPlayer Gamepad
    { 0x2dc8, 0x3106 }, // 8BitDo Ultimate Wireless / Pro 2 Wired Controller
    { 0x2dc8, 0x3109 }, // 8BitDo Ultimate Wireless Bluetooth
    { 0x2dc8, 0x3110 }, // 8BitDo Ultimate 2 Wireless PC
    { 0x2dc8, 0x3111 }, // 8BitDo Ultimate 2 Wireless variant
    { 0x2dc8, 0x310b }, // 8BitDo Ultimate 2 Wireless for PC，现场协议分析确认
    { 0x31e3, 0x1100 }, // Wooting One
    { 0x31e3, 0x1200 }, // Wooting Two
    { 0x31e3, 0x1210 }, // Wooting Lekker
    { 0x31e3, 0x1220 }, // Wooting Two HE
    { 0x31e3, 0x1230 }, // Wooting Two HE (ARM)
    { 0x31e3, 0x1300 }, // Wooting 60HE (AVR)
    { 0x31e3, 0x1310 }, // Wooting 60HE (ARM)
    { 0x3285, 0x0607 }, // Nacon GC-100
    { 0x413d, 0x2104 }, // Black Shark Green Ghost Gamepad
    { 0x0000, 0x0000 }  // end of list
};

/*
 * 8BitDo 接收器“宽松匹配”白名单：
 * 仅放宽接口 class/subclass/protocol，不放宽接口号。
 * 这里仍强制 interface number == 0（MI_00），避免误绑到 MI_01/MI_02。
 *
 * 背景：
 * 某些上电时序下（接收器先上电、手柄后开机），接收器可能先以非标准接口描述出现，
 * 之后才切到可用链路。若严格要求 FF/5D/01，会出现 device_present=1 但 xbox_active=0。
 * 这里先让已知 8BitDo 机型进入 xbox driver，后续由 sideband + report 链路拉起。
 */
static const uint16_t xbox_8bitdo_relaxed_id_table[][2] = {
    { 0x2dc8, 0x3106 }, // 8BitDo Ultimate Wireless / Pro 2 Wired Controller
    { 0x2dc8, 0x3109 }, // 8BitDo Ultimate Wireless Bluetooth
    { 0x2dc8, 0x3110 }, // 8BitDo Ultimate 2 Wireless PC
    { 0x2dc8, 0x3111 }, // 8BitDo Ultimate 2 Wireless variant
    { 0x2dc8, 0x310b }, // 8BitDo Ultimate 2 Wireless for PC
    { 0x0000, 0x0000 }  // end of list
};

CLASS_INFO_DEFINE const struct usbh_class_info xbox_custom_class_info = {
    .match_flags = USB_CLASS_MATCH_VID_PID | USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass = USB_DEVICE_CLASS_VEND_SPECIFIC,
    .bInterfaceSubClass = 0x5d,
    .bInterfaceProtocol = 0x01,
    .id_table =  xbox_id_table,
    .class_driver = &xbox_class_driver
};

CLASS_INFO_DEFINE const struct usbh_class_info xbox_8bitdo_relaxed_class_info = {
    .match_flags = USB_CLASS_MATCH_VID_PID | USB_CLASS_MATCH_INTF_NUM,
    .bInterfaceClass = 0x00,
    .bInterfaceSubClass = 0x00,
    .bInterfaceProtocol = 0x00,
    .bInterfaceNumber = 0x00,
    .id_table = xbox_8bitdo_relaxed_id_table,
    .class_driver = &xbox_class_driver
};

#if defined(CONFIG_USBHOST_XBOX_GENERIC_MATCH) && (CONFIG_USBHOST_XBOX_GENERIC_MATCH == 1)
/* 白名单优先，接口特征兜底：这样新型号 8BitDo 不在列表里时也能先跑起来，
 * 但仍限定在 FF/5D/01，避免误绑到普通 HID 接口。 */
CLASS_INFO_DEFINE const struct usbh_class_info xbox_generic_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass = USB_DEVICE_CLASS_VEND_SPECIFIC,
    .bInterfaceSubClass = 0x5d,
    .bInterfaceProtocol = 0x01,
    .id_table = NULL,
    .class_driver = &xbox_class_driver
};
#endif
