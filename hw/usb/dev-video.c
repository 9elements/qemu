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
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "hw/usb/video.h"
#include "desc.h"
#include "trace.h"

#define USBVIDEO_VENDOR   0x46f4 /* CRC16() of "QEMU" */
#define USBVIDEO_PRODUCT  0x0e01

enum usb_video_strings {
    STRING_NULL,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_SERIALNUMBER,
    STRING_CONFIG,
    STRING_INTERFACE_ASSOCIATION,
    STRING_VIDEO_CONTROL,
    STRING_INPUT_TERMINAL,
    STRING_SELECTOR_UNIT,
    STRING_PROCESSING_UNIT,
    STRING_OUTPUT_TERMINAL,
    STRING_VIDEO_STREAMING_OFF,
    STRING_VIDEO_STREAMING_ON,
};

static const USBDescStrings usb_video_stringtable = {
    [STRING_MANUFACTURER]               = "QEMU",
    [STRING_PRODUCT]                    = "QEMU USB Video",
    [STRING_SERIALNUMBER]               = "1",
    [STRING_CONFIG]                     = "Video Configuration",
    [STRING_INTERFACE_ASSOCIATION]      = "Integrated Camera",
    [STRING_VIDEO_CONTROL]              = "Video Control",
    [STRING_INPUT_TERMINAL]             = "Video Input Terminal",
    [STRING_SELECTOR_UNIT]              = "Video Selector Unit",
    [STRING_PROCESSING_UNIT]            = "Video Processing Unit",
    [STRING_OUTPUT_TERMINAL]            = "Video Output Terminal",
    [STRING_VIDEO_STREAMING_OFF]        = "Video Streaming off",
    [STRING_VIDEO_STREAMING_ON]         = "Video Streaming on",
};

/* Interface IDs */
#define IF_CONTROL   0x0
#define IF_STREAMING 0x1

/* Endpoint IDs */
#define EP_CONTROL   0x1
#define EP_STREAMING 0x2

/* Terminal IDs */
#define INPUT_TERMINAL  0x1
#define OUTPUT_TERMINAL 0x2

/* Alternate Settings */
#define ALTSET_OFF       0x0
#define ALTSET_STREAMING 0x1

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)
#define U24(x) U16(x), (((x) >> 16) & 0xff)
#define U32(x) U24(x), (((x) >> 24) & 0xff)

static const USBDescIface desc_ifaces[] = {
    {
        /* VideoControl Interface Descriptor */
        .bInterfaceNumber              = IF_CONTROL,
        .bAlternateSetting             = 0,
        .bNumEndpoints                 = 0,
        .bInterfaceClass               = USB_CLASS_VIDEO,
        .bInterfaceSubClass            = SC_VIDEOCONTROL,
        .bInterfaceProtocol            = PC_PROTOCOL_15,
        .iInterface                    = STRING_VIDEO_CONTROL,
        .ndesc                         = 3,
        .descs = (USBDescOther[]) {
            {
                /* Class-specific VC Interface Header Descriptor */
                .data = (uint8_t[]) {
                    0x0D,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VC_HEADER,               /*  u8  bDescriptorSubtype */
                    U16(0x0150),             /* u16  bcdUVC */
                    U16(0x0028),             /* u16  wTotalLength */
                    U32(0x005B8D80),         /* u32  dwClockFrequency */
                    0x01,                    /*  u8  bInCollection */
                    0x01,                    /*  u8  baInterfaceNr */
                }
            }, {
                /* Input Terminal Descriptor */
                .data = (uint8_t[]) {
                    0x12,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VC_INPUT_TERMINAL,       /*  u8  bDescriptorSubtype */
                    INPUT_TERMINAL,          /*  u8  bTerminalID */
                    U16(ITT_CAMERA),         /* u16  wTerminalType */
                    0x00,                    /*  u8  bAssocTerminal */
                    STRING_INPUT_TERMINAL,   /*  u8  iTerminal */
                    U16(0x0000),             /* u16  wObjectiveFocalLengthMin */
                    U16(0x0000),             /* u16  wObjectiveFocalLengthMax */
                    U16(0x0000),             /* u16  wOcularFocalLength */
                    0x03,                    /*  u8  bControlSize */
                    U24(0x000000),           /* u24  bmControls */
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
                    STRING_OUTPUT_TERMINAL,  /*  u8  iTerminal */
                }
            }
        },
    }, {
        /* VideoStreaming Interface Descriptor (Zero-bandwidth Alternate Setting 0) */
        .bInterfaceNumber              = IF_STREAMING,
        .bAlternateSetting             = ALTSET_OFF,
        .bNumEndpoints                 = 0,
        .bInterfaceClass               = USB_CLASS_VIDEO,
        .bInterfaceSubClass            = SC_VIDEOSTREAMING,
        .bInterfaceProtocol            = PC_PROTOCOL_15,
        .iInterface                    = STRING_VIDEO_STREAMING_OFF,
        .ndesc                         = 3,
        .descs = (USBDescOther[]) {
            {
                /* Class-specific VS Interface Input Header Descriptor */
                .data = (uint8_t[]) {
                    0x0E,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VS_INPUT_HEADER,         /*  u8  bDescriptorSubtype */
                    0x01,                    /*  u8  bNumFormats */
                    U16(0x47),               /*  u16 wTotalLength */
                    USB_DIR_IN|EP_STREAMING, /*  u8  bEndpointAddress */
                    0x00,                    /*  u8  bmInfo */
                    OUTPUT_TERMINAL,         /*  u8  bTerminalLink */
                    0x00,                    /*  u8  bStillCaptureMethod */
                    0x00,                    /*  u8  bTriggerSupport */
                    0x00,                    /*  u8  bTriggerUsage */
                    0x01,                    /*  u8  bControlSize */
                    0x00,                    /*  u8  bmaControls */
                }
            },
            {
                /* Class-specific VS Uncompressed YUY2 Format Descriptor */
                .data = (uint8_t[]) {
                    0x1B,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VS_FORMAT_UNCOMPRESSED,  /*  u8  bDescriptorSubtype */
                    0x01,                    /*  u8  bFormatIndex */
                    0x01,                    /*  u8  bNumFrameDescriptors */
                    /* guidFormat */
                     'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00,
                    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
                    0x10,                    /*  u8  bBitsPerPixel */
                    0x01,                    /*  u8  bDefaultFrameIndex */
                    0x00,                    /*  u8  bAspectRatioX */
                    0x00,                    /*  u8  bAspectRatioY */
                    0x00,                    /*  u8  bmInterlaceFlags */
                    0x00,                    /*  u8  bCopyProtect */
                }
            },
            {
                /* Class-specific VS Uncompressed YUY2 Frame Descriptor */
                .data = (uint8_t[]) {
                    0x1E,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VS_FRAME_UNCOMPRESSED,   /*  u8  bDescriptorSubtype */
                    0x01,                    /*  u8   bFrameIndex */
                    0x00,                    /*  u8   bmCapabilities */
                    U16(640),                /*  u16  wWidth */
                    U16(480),                /*  u16  wHeight */
                    U32(147456000),          /*  u32  dwMinBitRate */
                    U32(147456000),          /*  u32  dwMaxBitRate */
                    U32(614400),             /*  u32  wMaxVideoFrameBufferSize */
                    U32(333333),             /*  u32  dwDefaultFrameInterval */
                    0x01,                    /*  u8   bFrameIntervalType */
                    U32(333333),             /*  u32  dwFrameInterval(0) */
                }
            }
        },
    }, {
        /* Operational Alternate Setting 1 */
        .bInterfaceNumber              = IF_STREAMING,
        .bAlternateSetting             = ALTSET_STREAMING,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_VIDEO,
        .bInterfaceSubClass            = SC_VIDEOSTREAMING,
        .bInterfaceProtocol            = PC_PROTOCOL_15,
        .iInterface                    = STRING_VIDEO_STREAMING_ON,
        .eps = (USBDescEndpoint[]) {
            {
                /* Standard VS Isochronous Video Data Endpoint Descriptor */
                .bEndpointAddress      = USB_DIR_IN | EP_STREAMING,
                .bmAttributes          = 0x05,
                .wMaxPacketSize        = 0x400,
                .bInterval             = 0x1,
            },
        },
    }
};

static const USBDescIfaceAssoc desc_if_groups[] = {
    {
        .bFirstInterface = IF_CONTROL,
        .bInterfaceCount = 2,
        .bFunctionClass = USB_CLASS_VIDEO,
        .bFunctionSubClass = SC_VIDEO_INTERFACE_COLLECTION,
        .bFunctionProtocol = PC_PROTOCOL_UNDEFINED,
        .iFunction = STRING_INTERFACE_ASSOCIATION,
        .nif = ARRAY_SIZE(desc_ifaces),
        .ifs = desc_ifaces,
    },
};

static const USBDescDevice desc_device_high = {
    .bcdUSB                        = 0x0200,
    .bDeviceClass                  = USB_CLASS_MISCELLANEOUS,
    .bDeviceSubClass               = 2,
    .bDeviceProtocol               = 1, /* Interface Association */
    .bMaxPacketSize0               = 0x40,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .iConfiguration        = STRING_CONFIG,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .bMaxPower             = 0x32,
            .nif_groups            = ARRAY_SIZE(desc_if_groups),
            .if_groups             = desc_if_groups,
        },
    },
};

static const USBDesc desc_video = {
    .id = {
        .idVendor          = USBVIDEO_VENDOR,
        .idProduct         = USBVIDEO_PRODUCT,
        .bcdDevice         = 0,
        .iManufacturer     = STRING_MANUFACTURER,
        .iProduct          = STRING_PRODUCT,
        .iSerialNumber     = STRING_SERIALNUMBER,
    },
    .high = &desc_device_high,
    .str  = usb_video_stringtable,
};

#define TYPE_USB_VIDEO "usb-video"
OBJECT_DECLARE_SIMPLE_TYPE(USBVideoState, USB_VIDEO)

struct USBVideoState {
    USBDevice dev;
};

static int usb_video_handle_uvc_control(USBDevice *dev, int request, int16_t value,
                                 uint16_t index, uint16_t length, uint8_t *data)
{
    USBBus *bus = usb_bus_from_device(dev);
    uint8_t query = request & 0xff;
    uint8_t cs = value >> 8;
    uint8_t intfnum = index & 0xff;
    uint8_t unit = index >> 8;
    int ret = -1;

    trace_usb_video_handle_uvc_control(bus->busnr, dev->addr, query, intfnum, unit, cs);

    switch (intfnum) {
    case IF_CONTROL:
        break;
    case IF_STREAMING:
        switch (cs) {
        case VS_PROBE_CONTROL:
        case VS_COMMIT_CONTROL:
                // XXX
                break;
        default:
            break;
        }
        break;
    }

    return ret;
}

static void usb_video_handle_control(USBDevice *dev, USBPacket *p,
                                    int request, int value, int index,
                                    int length, uint8_t *data)
{
    USBBus *bus = usb_bus_from_device(dev);
    int ret;

    trace_usb_video_handle_control(bus->busnr, dev->addr, request, value, index, length);

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case ClassInterfaceRequest | GET_CUR:
    case ClassInterfaceRequest | GET_MIN:
    case ClassInterfaceRequest | GET_MAX:
    case ClassInterfaceRequest | GET_RES:
    case ClassInterfaceRequest | GET_LEN:
    case ClassInterfaceRequest | GET_INFO:
    case ClassInterfaceRequest | GET_DEF:
    case ClassInterfaceOutRequest | SET_CUR:
        ret = usb_video_handle_uvc_control(dev, request, value, index, length, data);
        if (ret < 0) {
            goto fail;
        }
        break;
    default:
        goto fail;
    }

    p->actual_length = ret;
    p->status = USB_RET_SUCCESS;
    return;

fail:
    trace_usb_video_unsupported_control(bus->busnr, dev->addr, request, value, ret);
    p->status = USB_RET_STALL;
}

static Property usb_video_properties[] = {
  DEFINE_PROP_END_OF_LIST(),
};

static void usb_video_realize(USBDevice *dev, Error **errp) {
    USBVideoState *s = USB_VIDEO(dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->dev.opaque = s;
}

static void usb_video_class_init(ObjectClass *klass, void *data)
{
  DeviceClass *dc = DEVICE_CLASS(klass);
  USBDeviceClass *k = USB_DEVICE_CLASS(klass);

  dc->desc = "QEMU Video";
  k->product_desc = "QEMU Video";
  k->usb_desc = &desc_video;
  k->handle_attach = usb_desc_attach;
  k->realize = usb_video_realize;
  k->handle_control = usb_video_handle_control;

  device_class_set_props(dc, usb_video_properties);
}

static const TypeInfo usb_video_info = {
    .name = TYPE_USB_VIDEO,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBVideoState),
    .class_init = usb_video_class_init,
};

static void usb_video_register_types(void)
{
  type_register_static(&usb_video_info);
}

type_init(usb_video_register_types)
