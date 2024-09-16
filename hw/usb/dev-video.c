/*
 * UVC Device emulation, base on UVC specification 1.5
 *
 * Copyright 2024 9elements GmbH
 * Copyright 2021 Bytedance, Inc.
 *
 * Authors:
 *   Marcello Sylvester Bauer <marcello.bauer@9elements.com>
 *   zhenwei pi <pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/usb.h"
#include "hw/usb/video.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "video/video.h"

#include "desc.h"
#include "trace.h"

#define USBVIDEO_VENDOR_NUM     0x46f4 /* CRC16() of "QEMU" */
#define USBVIDEO_PRODUCT_NUM    0x0001

/* Interface IDs */
#define IF_CONTROL   0x0
#define IF_STREAMING 0x1

/* Endpoint IDs */
#define EP_CONTROL   0x1
#define EP_STREAMING 0x2

/* Terminal and Unit IDs */
#define INPUT_TERMINAL  0x1
#define OUTPUT_TERMINAL 0x2

/* Alternate Settings */
#define ALTSET_OFF       0x0
#define ALTSET_STREAMING 0x1

enum usb_video_strings {
    STR_NULL,
    STR_MANUFACTURER,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_CONFIG_FULL,
    STR_CONFIG_HIGH,
    STR_CONFIG_SUPER,
    STR_VIDEO_CONTROL,
    STR_INPUT_TERMINAL,
    STR_OUTPUT_TERMINAL,
    STR_VIDEO_STREAMING,
    STR_VIDEO_STREAMING_ALTERNATE1,
};

static const USBDescStrings usb_video_stringtable = {
    [STR_MANUFACTURER]               = "QEMU",
    [STR_PRODUCT]                    = "QEMU USB Video",
    [STR_SERIALNUMBER]               = "1",
    [STR_CONFIG_FULL]                = "Full speed config (usb 1.1)",
    [STR_CONFIG_HIGH]                = "High speed config (usb 2.0)",
    [STR_CONFIG_SUPER]               = "Super speed config (usb 3.0)",
    [STR_VIDEO_CONTROL]              = "Video Control",
    [STR_INPUT_TERMINAL]             = "Video Input Terminal",
    [STR_OUTPUT_TERMINAL]            = "Video Output Terminal",
    [STR_VIDEO_STREAMING]            = "Video Streaming",
    [STR_VIDEO_STREAMING_ALTERNATE1] = "Video Streaming Alternate Setting 1",
};

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)
#define U24(x) U16(x), (((x) >> 16) & 0xff)
#define U32(x) U24(x), (((x) >> 24) & 0xff)

static const USBDescIfaceAssoc desc_if_groups[] = {
    {
        .bFirstInterface = IF_CONTROL,
        .bInterfaceCount = 2,
        .bFunctionClass = USB_CLASS_VIDEO,
        .bFunctionSubClass = SC_VIDEO_INTERFACE_COLLECTION,
        .bFunctionProtocol = PC_PROTOCOL_UNDEFINED,
    },
};

static const USBDescOther vc_iface_descs[] = {
    {
        /* Class-specific VS Interface Input Header Descriptor */
        .data = (uint8_t[]) {
            0x0D,                    /*  u8  bLength */
            CS_INTERFACE,            /*  u8  bDescriptorType */
            VC_HEADER,               /*  u8  bDescriptorSubtype */
            U16(0x0110),             /* u16  bcdADC */
            U16(0x0034),             /* u16  wTotalLength */
            U32(0x005B8D80),         /* u32  dwClockFrequency */
            0x01,                    /*  u8  bInCollection */
            0x01,                    /*  u8  baInterfaceNr */
        }
    }, {
        /* Input Terminal Descriptor (Camera) */
        .data = (uint8_t[]) {
            0x11,                    /*  u8  bLength */
            CS_INTERFACE,            /*  u8  bDescriptorType */
            VC_INPUT_TERMINAL,       /*  u8  bDescriptorSubtype */
            INPUT_TERMINAL,          /*  u8  bTerminalID */
            U16(ITT_CAMERA),         /* u16  wTerminalType */
            0x00,                    /*  u8  bAssocTerminal */
            STR_INPUT_TERMINAL,      /*  u8  iTerminal */
            U16(0x0000),             /* u16  wObjectiveFocalLengthMin */
            U16(0x0000),             /* u16  wObjectiveFocalLengthMax */
            U16(0x0000),             /* u16  wOcularFocalLength */
            0x02,                    /*  u8  bControlSize */
            U16(0x0000),             /* u16  bmControls */
        }
    }, {
        /* Output Terminal Descriptor */
        .data = (uint8_t[]) {
            0x09,                    /*  u8  bLength */
            CS_INTERFACE,            /*  u8  bDescriptorType */
            VC_OUTPUT_TERMINAL,      /*  u8  bDescriptorSubtype */
            OUTPUT_TERMINAL,         /*  u8  bTerminalID */
            U16(TT_STREAMING),       /* u16  wTerminalType */
            0x00,                    /*  u8  bAssocTerminal */
            INPUT_TERMINAL,          /*  u8  bSourceID */
            STR_OUTPUT_TERMINAL,     /*  u8  iTerminal */
        }
    }
};

static const USBDescEndpoint vc_iface_eps[] = {
    {
        .bEndpointAddress = USB_DIR_IN | EP_CONTROL,
        .bmAttributes     = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize   = 0x40,
        .bInterval        = 0x20,
    },
};

static const USBDescEndpoint vs_iface_eps[] = {
    {
        .bEndpointAddress = USB_DIR_IN | EP_STREAMING,
        .bmAttributes     = 0x05,
        .wMaxPacketSize   = 1024,
        .bInterval        = 0x1,
    },
};

enum video_desc_iface_idx {
    VC = 0,
    VS0,
    VS1,
    USB_VIDEO_IFACE_COUNT
};

static const USBDescIface *usb_video_desc_iface_new(USBDevice *dev)
{

    USBDescIface *d = g_new0(USBDescIface, USB_VIDEO_IFACE_COUNT);

    d[VC].bInterfaceNumber   = IF_CONTROL;
    d[VC].bInterfaceClass    = USB_CLASS_VIDEO;
    d[VC].bInterfaceSubClass = SC_VIDEOCONTROL;
    d[VC].bInterfaceProtocol = PC_PROTOCOL_15;
    d[VC].iInterface         = STR_VIDEO_CONTROL;
    d[VC].ndesc              = ARRAY_SIZE(vc_iface_descs);
    d[VC].descs              = (USBDescOther *) &vc_iface_descs;
    d[VC].bNumEndpoints      = ARRAY_SIZE(vc_iface_eps);
    d[VC].eps                = (USBDescEndpoint *)vc_iface_eps;

    d[VS0].bInterfaceNumber   = IF_STREAMING;
    d[VS0].bAlternateSetting  = ALTSET_OFF;
    d[VS0].bNumEndpoints      = 0;
    d[VS0].bInterfaceClass    = USB_CLASS_VIDEO;
    d[VS0].bInterfaceSubClass = SC_VIDEOSTREAMING;
    d[VS0].bInterfaceProtocol = PC_PROTOCOL_15;
    d[VS0].iInterface         = STR_VIDEO_STREAMING;

    d[VS1].bInterfaceNumber   = IF_STREAMING;
    d[VS1].bAlternateSetting  = ALTSET_STREAMING;
    d[VS1].bNumEndpoints      = 0;
    d[VS1].bInterfaceClass    = USB_CLASS_VIDEO;
    d[VS1].bInterfaceSubClass = SC_VIDEOSTREAMING;
    d[VS1].bInterfaceProtocol = PC_PROTOCOL_15;
    d[VS1].iInterface         = STR_VIDEO_STREAMING_ALTERNATE1;
    d[VS1].bNumEndpoints      = ARRAY_SIZE(vs_iface_eps);
    d[VS1].eps                = (USBDescEndpoint *)vs_iface_eps;

    return d;
}

static const USBDescDevice *usb_video_desc_device_new(USBDevice *dev, const USBDescIface *ifaces, int speed)
{

    USBDescDevice *d = g_new0(USBDescDevice, 1);
    USBDescConfig *c = g_new0(USBDescConfig, 1);

    d->bDeviceClass        = USB_CLASS_MISCELLANEOUS;
    d->bDeviceSubClass     = 2;
    d->bDeviceProtocol     = 1;
    d->bMaxPacketSize0     = 8;
    d->bNumConfigurations  = 1;

    d->confs = c;
    c->bNumInterfaces      = 2;
    c->bConfigurationValue = 1;
    c->bmAttributes        = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER;
    c->bMaxPower           = 0x32;
    c->nif_groups          = ARRAY_SIZE(desc_if_groups);
    c->if_groups           = desc_if_groups;
    c->nif                 = USB_VIDEO_IFACE_COUNT;
    c->ifs                 = ifaces;

    switch (speed) {
    case USB_SPEED_FULL:
        d->bcdUSB          = 0x0200;
        c->iConfiguration  = STR_CONFIG_FULL;
        break;
    case USB_SPEED_HIGH:
        d->bcdUSB          = 0x0200;
        c->iConfiguration  = STR_CONFIG_HIGH;
        break;
    case USB_SPEED_SUPER:
        d->bcdUSB          = 0x0300;
        c->iConfiguration  = STR_CONFIG_SUPER;
        break;
    }

    return d;
}

static void usb_video_desc_new(USBDevice *dev)
{
    USBDesc *d;
    const USBDescIface *i;

    d = g_new0(USBDesc, 1);
    d->id.idVendor      = USBVIDEO_VENDOR_NUM;
    d->id.idProduct     = USBVIDEO_PRODUCT_NUM;
    d->id.iManufacturer = STR_MANUFACTURER;
    d->id.iProduct      = STR_PRODUCT;
    d->id.iSerialNumber = STR_SERIALNUMBER;
    d->str = usb_video_stringtable;

    i = usb_video_desc_iface_new(dev);
    d->full  = usb_video_desc_device_new(dev, i, USB_SPEED_FULL);
    d->high  = usb_video_desc_device_new(dev, i, USB_SPEED_HIGH);
    d->super = usb_video_desc_device_new(dev, i, USB_SPEED_SUPER);

    dev->usb_desc = d;
}

static void usb_video_desc_free(USBDevice *dev)
{
    const USBDesc *d = dev->usb_desc;
    g_free((void *)d->full->confs->ifs);
    g_free((void *)d->full->confs);
    g_free((void *)d->high->confs);
    g_free((void *)d->super->confs);
    g_free((void *)d->full);
    g_free((void *)d->high);
    g_free((void *)d->super);

    dev->usb_desc = NULL;
}


static void usb_video_handle_data_control_in(USBDevice *dev, USBPacket *p)
{
    USBBus *bus = usb_bus_from_device(dev);
    int len = 0;

    trace_usb_video_handle_data_control_in(bus->busnr, dev->addr, len);

    p->status = USB_RET_STALL;
}

static void usb_video_handle_data_streaming_in(USBDevice *dev, USBPacket *p)
{
    USBBus *bus = usb_bus_from_device(dev);
    int len = 0;

    trace_usb_video_handle_data_streaming_in(bus->busnr, dev->addr, len);

    p->status = USB_RET_STALL;
}


struct USBVideoState {
    /* qemu interfaces */
    USBDevice dev;
    Videodev *video;
};

#define TYPE_USB_VIDEO "usb-video"
OBJECT_DECLARE_SIMPLE_TYPE(USBVideoState, USB_VIDEO)

static void usb_video_realize(USBDevice *dev, Error **errp)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBVideoState *s = USB_VIDEO(dev);

    trace_usb_video_realize(bus->busnr, dev->addr);

    if (!s->video) {
        error_setg(errp, QERR_MISSING_PARAMETER, "videodev");
        return;
    }

    usb_video_desc_new(dev);
    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->dev.opaque = s;
}

static void usb_video_handle_reset(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);

    trace_usb_video_handle_reset(bus->busnr, dev->addr);
}


static void usb_video_handle_control(USBDevice *dev, USBPacket *p,
                                    int request, int value, int index,
                                    int length, uint8_t *data)
{
    int ret;
    USBBus *bus = usb_bus_from_device(dev);

    trace_usb_video_handle_control(bus->busnr, dev->addr, request, value);

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    default:
        goto error;
    }

    return;

error:
    trace_usb_video_handle_control_error(bus->busnr, dev->addr, request,
        value, index, length);
    p->status = USB_RET_STALL;
}

static void usb_video_handle_data(USBDevice *dev, USBPacket *p)
{
    if ((p->pid == USB_TOKEN_IN) && (p->ep->nr == EP_STREAMING)) {
        usb_video_handle_data_streaming_in(dev, p);
        return;
    } else if ((p->pid == USB_TOKEN_IN) && (p->ep->nr == EP_CONTROL)) {
        usb_video_handle_data_control_in(dev, p);
        return;
    }

    p->status = USB_RET_STALL;
}

static void usb_video_set_interface(USBDevice *dev, int iface,
                                    int old, int value)
{
    USBBus *bus = usb_bus_from_device(dev);

    trace_usb_video_set_interface(bus->busnr, dev->addr, iface, value);
}

static void usb_video_unrealize(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    trace_usb_video_unrealize(bus->busnr, dev->addr);
    //USBVideoState *s = USB_VIDEO(dev);

    usb_video_desc_free(dev);
}

static Property usb_video_properties[] = {
    DEFINE_VIDEO_PROPERTIES(USBVideoState, video),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_video_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *k = USB_DEVICE_CLASS(klass);

    device_class_set_props(dc, usb_video_properties);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    k->product_desc   = "QEMU USB Video Interface";
    k->realize        = usb_video_realize;
    k->handle_control = usb_video_handle_control;
    k->handle_reset   = usb_video_handle_reset;
    k->handle_data    = usb_video_handle_data;
    k->unrealize      = usb_video_unrealize;
    k->set_interface  = usb_video_set_interface;
}

static const TypeInfo usb_video_info = {
    .name          = TYPE_USB_VIDEO,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBVideoState),
    .class_init    = usb_video_class_init,
};

static void usb_video_register_types(void)
{
    type_register_static(&usb_video_info);
}

type_init(usb_video_register_types)
