/*
 * UVC Device emulation, base on UVC specification 1.5
 *
 * Copyright 2024 9elements GmbH
 * Copyright 2021 Bytedance, Inc.
 *
 * Authors:
 *   David Milosevic <david.milosevic@9elements.com>
 *   Marcello Sylvester Bauer <marcello.bauer@9elements.com>
 *   zhenwei pi <pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/usb.h"
#include "hw/usb/video.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "video/video.h"

#include "desc.h"
#include "trace.h"

enum AttributeIndex {
    ATTRIBUTE_DEF,
    ATTRIBUTE_MIN,
    ATTRIBUTE_MAX,
    ATTRIBUTE_CUR,
    ATTRIBUTE_RES,
    ATTRIBUTE_ALL
};

typedef struct USBVideoControlStats {
    VideoControlStatus status;
    uint8_t size; /* value size in bytes */
    QTAILQ_ENTRY(USBVideoControlStats) list;
} USBVideoControlStats;

typedef struct USBVideoControlInfo {
    uint8_t selector;
    uint8_t caps;
    uint8_t size;
    uint32_t value[ATTRIBUTE_ALL]; /* stored in le32 */
} USBVideoControlInfo;

struct USBVideoState {
    /* qemu interfaces */
    USBDevice dev;
    Videodev *video;

    /* UVC control */
    uint8_t error;
    USBVideoControlInfo pu_attrs[PU_MAX];
    QTAILQ_HEAD(, USBVideoControlStats) control_status;

    /* video streaming control */
    uint8_t vsc_info;
    uint16_t vsc_len;
    VideoStreamingControl vsc_attrs[ATTRIBUTE_ALL];
};

#define TYPE_USB_VIDEO "usb-video"
OBJECT_DECLARE_SIMPLE_TYPE(USBVideoState, USB_VIDEO)

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

/* XU IDs */
#define SELECTOR_UNIT   0x4
#define PROCESSING_UNIT 0x5
#define ENCODING_UNIT   0x6

enum usb_video_strings {
    STR_NULL,
    STR_MANUFACTURER,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_CONFIG,
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
    [STR_CONFIG]                     = "Video Configuration",
    [STR_VIDEO_CONTROL]              = "Video Control",
    [STR_INPUT_TERMINAL]             = "Video Input Terminal",
    [STR_OUTPUT_TERMINAL]            = "Video Output Terminal",
    [STR_VIDEO_STREAMING]            = "Video Streaming",
    [STR_VIDEO_STREAMING_ALTERNATE1] = "Video Streaming Alternate Setting 1",
};

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)
#define U24(x) U16(x), (((x) >> 16) & 0xff)
#define U32(x) U24(x), (((x) >> 24) & 0xff)

#define REQ_TO_ATTR(req, idx)  \
    switch (req) {             \
    case SET_CUR:              \
    case GET_CUR:              \
        idx = ATTRIBUTE_CUR;   \
        break;                 \
    case GET_MIN:              \
        idx = ATTRIBUTE_MIN;   \
        break;                 \
    case GET_MAX:              \
        idx = ATTRIBUTE_MAX;   \
        break;                 \
    case GET_RES:              \
        idx = ATTRIBUTE_RES;   \
        break;                 \
    case GET_DEF:              \
        idx = ATTRIBUTE_DEF;   \
        break;                 \
    default:                   \
        idx = -1;              \
        break;                 \
    }

#define handle_get_control(attrs, req, cs, length, data, ret)                \
    do {                                                                     \
        if (!attrs[cs].selector) {                                           \
            break;                                                           \
        }                                                                    \
        if ((req == GET_INFO) && (length >= 1)) {                            \
            *((uint8_t *)data) = attrs[cs].caps;                             \
            ret = 1;                                                         \
        } else if ((req == GET_LEN) && (length >= 2)) {                      \
            *((uint16_t *)data) = cpu_to_le16(attrs[cs].size);               \
            ret = 2;                                                         \
        } else {                                                             \
            int idx = -1;                                                    \
            int len = MIN(length, sizeof(attrs[cs].size));                   \
            REQ_TO_ATTR(req, idx);                                           \
            if (idx >= 0) {                                                  \
                memcpy(data, &attrs[cs].value[idx], len);                    \
                ret = length;                                                \
            }                                                                \
        }                                                                    \
    } while (0)

#define handle_get_streaming(s, req, cs, length, data, ret)                  \
    do {                                                                     \
        if ((req == GET_INFO) && (length >= 1)) {                            \
            *((uint8_t *)data) = s->cs##_len;                                \
            ret = 1;                                                         \
        } else if ((req == GET_LEN) && (length >= 2)) {                      \
            *((uint16_t *)data) = cpu_to_le16(s->cs##_len);                  \
            ret = 2;                                                         \
        } else {                                                             \
            int idx = -1;                                                    \
            int len = MIN(length, sizeof(s->cs##_attrs[0]));                 \
            REQ_TO_ATTR(req, idx);                                           \
            if (idx >= 0) {                                                  \
                memcpy(data, s->cs##_attrs + idx, len);                      \
                ret = length;                                                \
            }                                                                \
        }                                                                    \
    } while (0)

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

#define VS_HEADER_LEN                  0xe
#define VS_FORMAT_UNCOMPRESSED_LEN     0x1b
#define VS_FORMAT_MJPEG_LEN            0xb
#define VS_FRAME_MIN_LEN 0x1a
#define VS_FRAME_SIZE(n)  (VS_FRAME_MIN_LEN+4*(n))

static void usb_video_parse_vs_header(USBDescOther *header, uint16_t wTotalLength)
{
    uint8_t *data;
    /* Class-specific VS Header Descriptor (Input) */
    uint8_t header_data[] = {
        VS_HEADER_LEN,              /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        VS_INPUT_HEADER,            /*  u8  bDescriptorSubtype */
        0x01,                       /*  u8  bNumFormats */
        U16(wTotalLength),          /* u16  wTotalLength */
        USB_DIR_IN | EP_STREAMING,  /*  u8  bEndPointAddress */
        0x00,                       /*  u8  bmInfo */
        OUTPUT_TERMINAL,            /*  u8  bTerminalLink */
        0x01,                       /*  u8  bStillCaptureMethod */
        0x01,                       /*  u8  bTriggerSupport */
        0x00,                       /*  u8  bTriggerUsage */
        0x01,                       /*  u8  bControlSize */
        0x00,                       /*  u8  bmaControls */
    };

    header->length = header_data[0];
    data = g_malloc0(header->length);
    memcpy(data, header_data, VS_HEADER_LEN);
    header->data = data;
}

static uint8_t usb_video_pixfmt_to_vsfmt(uint32_t pixfmt)
{
    switch (pixfmt) {
    case QEMU_VIDEO_PIX_FMT_YUYV:
    case QEMU_VIDEO_PIX_FMT_NV12:
        return VS_FORMAT_UNCOMPRESSED;
    }

    return VS_UNDEFINED;
}

static void usb_video_parse_vs_frame(USBDescIface *iface, VideoFramesize * frmsz, int frame_index, int *len)
{
    USBDescOther *desc;
    uint8_t *data, bLength = VS_FRAME_SIZE(frmsz->nframerate);
    uint16_t wWidth = frmsz->width;
    uint16_t wHeight = frmsz->height;
    // XXX: Parse from format descriptor
    uint8_t bDescriptorSubtype = VS_FRAME_UNCOMPRESSED;
    int i;
    uint32_t *ival;
    VideoFramerate frmival;
    uint8_t bFrameIntervalType = frmsz->nframerate;

    /* Class-specific VS Frame Descriptor */
    uint8_t frame_data[] = {
        bLength,                    /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        bDescriptorSubtype,         /*  u8  bDescriptorSubtype */
        frame_index,                /*  u8  bFrameIndex */
        0x03,                       /*  u8  bmCapabilities */
        U16(wWidth),                /* u16  wWidth */
        U16(wHeight),               /* u16  wHeight */
        U32(442368000),             /* u32  dwMinBitRate */
        U32(442368000),             /* u32  dwMaxBitRate */
        // XXX
        U32(0),                     /* u32  dwMaxVideoFrameBufSize */
        // XXX
        U32(0),                     /* u32  dwDefaultFrameInterval */
        bFrameIntervalType,         /*  u8  bFrameIntervalType */
    };

    iface->ndesc++;
    iface->descs = g_realloc(iface->descs,
                             iface->ndesc * sizeof(USBDescOther));
    desc = &iface->descs[iface->ndesc - 1];
    desc->length = frame_data[0];
    data = g_malloc0(frame_data[0]);
    memcpy(data, frame_data, VS_FRAME_MIN_LEN);
    desc->data = data;
    *len += desc->length;

    for (i = 0; i < bFrameIntervalType; i++) {
        frmival = frmsz->framerates[i];
        ival = (uint32_t *)((void*)data + VS_FRAME_MIN_LEN + 4 * i);
        *ival = cpu_to_le32(10000000 * frmival.numerator / frmival.denominator);
    }
}

static void usb_video_parse_vs_format(USBDescIface *iface, VideoMode *mode, int format_index, int *len)
{
    int i;
    USBDescOther *desc;
    uint8_t *data, *format_data;
    uint8_t bDescriptorSubtype = usb_video_pixfmt_to_vsfmt(mode->pixelformat);
    uint8_t bNumFrameDescriptors = mode->nframesize;

    uint8_t yuyv_fmt[] = {
        VS_FORMAT_UNCOMPRESSED_LEN, /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        bDescriptorSubtype,         /*  u8  bDescriptorSubtype */
        format_index,               /*  u8  bFormatIndex */
        bNumFrameDescriptors,       /*  u8  bNumFrameDescriptors */
        /* guidFormat */
        'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
        0x10,                       /*  u8  bBitsPerPixel */
        0x01,                       /*  u8  bDefaultFrameIndex */
        0x00,                       /*  u8  bAspectRatioX */
        0x00,                       /*  u8  bAspectRatioY */
        0x00,                       /*  u8  bmInterlaceFlags */
        0x00,                       /*  u8  bCopyProtect */
    };

    uint8_t nv12_fmt[] = {
        VS_FORMAT_UNCOMPRESSED_LEN, /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        bDescriptorSubtype,         /*  u8  bDescriptorSubtype */
        format_index,               /*  u8  bFormatIndex */
        bNumFrameDescriptors,       /*  u8  bNumFrameDescriptors */
        /* guidFormat */
        'N',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
        0x10,                       /*  u8  bBitsPerPixel */
        0x01,                       /*  u8  bDefaultFrameIndex */
        0x00,                       /*  u8  bAspectRatioX */
        0x00,                       /*  u8  bAspectRatioY */
        0x00,                       /*  u8  bmInterlaceFlags */
        0x00,                       /*  u8  bCopyProtect */
    };

    assert(qemu_video_pixfmt_supported(mode->pixelformat));
    switch(mode->pixelformat) {
    case QEMU_VIDEO_PIX_FMT_YUYV:
        format_data = yuyv_fmt;
        break;
    case QEMU_VIDEO_PIX_FMT_NV12:
        format_data = nv12_fmt;
        break;
    }

    iface->ndesc++;
    iface->descs = g_realloc(iface->descs,
                             iface->ndesc * sizeof(USBDescOther));
    desc = &iface->descs[iface->ndesc - 1];
    desc->length = format_data[0];
    data = g_malloc0(desc->length);
    memcpy(data, format_data, format_data[0]);
    desc->data = data;
    *len += desc->length;

    for (i = 0; i < bNumFrameDescriptors; i++) {
        usb_video_parse_vs_frame(iface, &mode->framesizes[i], i + 1, len);
    }
}

static void usb_video_parse_vs_desc(USBVideoState *s, USBDescIface *iface)
{
    int i, len;

    assert(s->video);
    assert(iface->descs == NULL);
    assert(iface->ndesc == 0);

    // parse the header descriptors once we know the total size.
    len = VS_HEADER_LEN;
    iface->ndesc = 1;
    iface->descs = g_new0(USBDescOther, iface->ndesc);

    // parse all formats
    for (i = 0; i < s->video->nmode; i++) {
        usb_video_parse_vs_format(iface, &s->video->modes[i], i + 1, &len);
    }

    usb_video_parse_vs_header(&iface->descs[0], len);
}

enum video_desc_iface_idx {
    VC = 0,
    VS0,
    VS1,
    USB_VIDEO_IFACE_COUNT
};

static const USBDescIface *usb_video_desc_iface_new(USBDevice *dev)
{

    USBVideoState *s = USB_VIDEO(dev);
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

    usb_video_parse_vs_desc(s, &d[VS0]);

    return d;
}

static const USBDescDevice *usb_video_desc_device_new(USBDevice *dev,
                                                      const uint16_t bcdUSB,
                                                      const uint8_t bMaxPacketSize0)
{
    USBDescDevice *d = g_new0(USBDescDevice, 1);
    USBDescConfig *c = g_new0(USBDescConfig, 1);

    d->bcdUSB              = bcdUSB;
    d->bDeviceClass        = USB_CLASS_MISCELLANEOUS;
    d->bDeviceSubClass     = 2;
    d->bDeviceProtocol     = 1;
    d->bMaxPacketSize0     = bMaxPacketSize0;
    d->bNumConfigurations  = 1;

    d->confs = c;
    c->bNumInterfaces      = 2;
    c->bConfigurationValue = 1;
    c->iConfiguration      = STR_CONFIG;
    c->bmAttributes        = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER;
    c->bMaxPower           = 0x32;
    c->nif_groups          = ARRAY_SIZE(desc_if_groups);
    c->if_groups           = desc_if_groups;
    c->nif                 = USB_VIDEO_IFACE_COUNT;
    c->ifs                 = usb_video_desc_iface_new(dev);

    return d;
}

static void usb_video_desc_new(USBDevice *dev)
{
    USBDesc *d;

    d = g_new0(USBDesc, 1);
    d->id.idVendor      = USBVIDEO_VENDOR_NUM;
    d->id.idProduct     = USBVIDEO_PRODUCT_NUM;
    d->id.iManufacturer = STR_MANUFACTURER;
    d->id.iProduct      = STR_PRODUCT;
    d->id.iSerialNumber = STR_SERIALNUMBER;
    d->str              = usb_video_stringtable;
    d->full             = usb_video_desc_device_new(dev, 0x0100, 8);
    d->high             = usb_video_desc_device_new(dev, 0x0200, 64);

    dev->usb_desc = d;
}

static void usb_video_desc_free(USBDevice *dev)
{
    const USBDesc *d = dev->usb_desc;
    g_free((void *)d->full->confs->ifs);
    g_free((void *)d->full->confs);
    // todo: g_free((void *)d->high->confs->ifs);
    g_free((void *)d->high->confs);
    g_free((void *)d->super->confs);
    g_free((void *)d->full);
    g_free((void *)d->high);
    g_free((void *)d->super);

    dev->usb_desc = NULL;
}

static VideodevControlType usb_video_pu_control_type_to_qemu(uint8_t cs)
{
    switch (cs) {
    case PU_BRIGHTNESS_CONTROL:
        return VideodevBrightness;
    case PU_CONTRAST_CONTROL:
        return VideodevContrast;
    case PU_GAIN_CONTROL:
        return VideodevGain;
    case PU_GAMMA_CONTROL:
        return VideodevGamma;
    case PU_HUE_CONTROL:
        return VideodevHue;
    case PU_HUE_AUTO_CONTROL:
        return VideodevHueAuto;
    case PU_SATURATION_CONTROL:
        return VideodevSaturation;
    case PU_SHARPNESS_CONTROL:
        return VideodevSharpness;
    case PU_WHITE_BALANCE_TEMPERATURE_CONTROL:
        return VideodevWhiteBalanceTemperature;
    }

    return VideodevControlMax;
}

static uint32_t usb_video_vsfmt_to_pixfmt(const uint8_t *data)
{
    uint8_t bDescriptorSubtype = data[2];
    uint32_t pixfmt = 0;

    switch (bDescriptorSubtype) {
    case VS_FORMAT_MJPEG:
        return QEMU_VIDEO_PIX_FMT_MJPEG;

    case VS_FORMAT_UNCOMPRESSED:
        pixfmt = *(uint32_t *)(data + 5);
        if (pixfmt == fourcc_code('Y', 'U', 'Y', '2')) {
            return QEMU_VIDEO_PIX_FMT_YUYV;
        } else if (pixfmt == fourcc_code('R', 'G', 'B', 'P')) {
            return QEMU_VIDEO_PIX_FMT_RGB565;
        }
    }

    return 0;
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

static uint32_t usb_video_get_max_framesize(Videodev *video)
{
    uint32_t max_framesize = 0;

    for (int i = 0; i < video->nmode; i++) {

        VideoMode *mode = &video->modes[i];

        for (int j = 0; j < mode->nframesize; j++) {

            const uint32_t height = mode->framesizes[j].height;
            const uint32_t width  = mode->framesizes[j].width;

            if (height * width * 2 > max_framesize)
                max_framesize = height * width * 2;
        }
    }

    return max_framesize;
}

static int usb_video_initialize(USBDevice *dev)
{
    USBVideoState *s = USB_VIDEO(dev);
    VideoStreamingControl *vsc;

    /*
     * build USB descriptors
     * */

    usb_video_desc_new(dev);
    usb_desc_create_serial(dev);
    usb_desc_init(dev);

    /*
     * initialize processing unit attributes
     * */

    // todo

    /*
     * initialize video streaming control attributes
     * */

    s->vsc_info = 0;
    s->vsc_len  = sizeof(VideoStreamingControl);

    vsc = &s->vsc_attrs[ATTRIBUTE_DEF];

    vsc->bFormatIndex             = 1;
    vsc->bFrameIndex              = 1;
    vsc->dwFrameInterval          = cpu_to_le32(1000000); // default 10 FPS
    vsc->wDelay                   = cpu_to_le16(32);
    vsc->dwMaxVideoFrameSize      = cpu_to_le32(usb_video_get_max_framesize(s->video));
    vsc->dwMaxPayloadTransferSize = cpu_to_le32(1024);
    vsc->dwClockFrequency         = cpu_to_le32(15000000);

    memcpy(&s->vsc_attrs[ATTRIBUTE_CUR], vsc, sizeof(VideoStreamingControl));
    memcpy(&s->vsc_attrs[ATTRIBUTE_MIN], vsc, sizeof(VideoStreamingControl));
    memcpy(&s->vsc_attrs[ATTRIBUTE_MAX], vsc, sizeof(VideoStreamingControl));

    return 0;
}

static void usb_video_realize(USBDevice *dev, Error **errp)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBVideoState *s = USB_VIDEO(dev);

    trace_usb_video_realize(bus->busnr, dev->addr);

    if (!s->video) {
        error_setg(errp, QERR_MISSING_PARAMETER, "videodev");
        return;
    }

    if (usb_video_initialize(dev) < 0) {
        error_setg(errp, "%s: Could not initialize USB video", TYPE_USB_VIDEO);
        return;
    }

    QTAILQ_INIT(&s->control_status);

    s->dev.opaque = s;
    s->error = 0;
}

static void usb_video_handle_reset(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);

    trace_usb_video_handle_reset(bus->busnr, dev->addr);
}

static void usb_video_queue_control_status(USBDevice *dev, uint8_t bOriginator,
                                           uint8_t bSelector, uint32_t *value, uint8_t size)
{
    USBVideoState *s = USB_VIDEO(dev);
    // USBBus *bus = usb_bus_from_device(dev);
    USBVideoControlStats *usb_status;
    VideoControlStatus *status;

    usb_status = g_malloc0(sizeof(USBVideoControlStats));
    usb_status->size = size;
    status = &usb_status->status;
    status->bStatusType = STATUS_INTERRUPT_CONTROL;
    status->bOriginator = bOriginator;
    status->bEvent = 0;
    status->bSelector = bSelector;
    status->bAttribute = STATUS_CONTROL_VALUE_CHANGE;
    memcpy(status->bValue, value, size);

    QTAILQ_INSERT_TAIL(&s->control_status, usb_status, list);
    // trace_usb_video_queue_control_status(bus->busnr, dev->addr, bOriginator,
    //                                      bSelector, *value, size);
}


static int usb_video_set_vs_control(USBDevice *dev, uint8_t req, int length, uint8_t *data)
{
    USBVideoState *s = USB_VIDEO(dev);
    int idx = -1;
    int ret = USB_RET_STALL;

    REQ_TO_ATTR(req, idx);
    if ((idx >= 0) && (length <= sizeof(s->vsc_attrs[0]))) {
        VideoStreamingControl *dst = s->vsc_attrs + idx;
        VideoStreamingControl *src = (VideoStreamingControl *)data;

        dst->bFormatIndex = src->bFormatIndex;
        dst->bFrameIndex = src->bFrameIndex;
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, dwFrameInterval, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wKeyFrameRate, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wPFrameRate, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wCompQuality, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wCompWindowSize, src, dst);
        ret = length;
    }

    return ret;
}

static int usb_video_get_frmi_from_vsc(USBDevice *dev,
                                       VideoStreamingControl *vsc,
                                       VideoFrameInterval *frmi)
{
    const USBDesc *desc = usb_device_get_usb_desc(dev);
    const USBDescIface *vs_iface = &desc->full->confs->ifs[VS0];
    USBDescOther *usb_desc;
    uint32_t pixfmt = 0;
    uint16_t width = 0, height = 0;
    uint8_t bDescriptorSubtype;
    uint8_t index;

    /* 1, search bFormatIndex */
    for (index = 0; index < vs_iface->ndesc; index++) {
        usb_desc = vs_iface->descs + index;
        if (usb_desc->data[0] < 4) {
            return -ENODEV;
        }

        bDescriptorSubtype = usb_desc->data[2];
        if ((bDescriptorSubtype == VS_FORMAT_MJPEG)
           || (bDescriptorSubtype == VS_FORMAT_UNCOMPRESSED)) {
            if (usb_desc->data[3] == vsc->bFormatIndex) {
                pixfmt = usb_video_vsfmt_to_pixfmt(usb_desc->data);
                break;
            }
        }
    }

    /* 2, search bFormatIndex */
    for (index++ ; pixfmt && index < vs_iface->ndesc; index++) {
        usb_desc = vs_iface->descs + index;
        if (usb_desc->data[0] < 4) {
            return -ENODEV;
        }

        bDescriptorSubtype = usb_desc->data[2];
        if ((bDescriptorSubtype == VS_FRAME_MJPEG)
           || (bDescriptorSubtype == VS_FRAME_UNCOMPRESSED)) {
            if (usb_desc->data[3] == vsc->bFrameIndex) {
                /* see Class-specific VS Frame Descriptor */
                width = le16_to_cpu(*(uint16_t *)(usb_desc->data + 5));
                height = le16_to_cpu(*(uint16_t *)(usb_desc->data + 7));
                break;
            }
        } else {
            break;
        }
    }

    if (pixfmt && width && height) {

        frmi->pixel_format = pixfmt;
        frmi->width = width;
        frmi->height = height;
        frmi->type = VIDEO_FRMIVAL_TYPE_DISCRETE;
        frmi->d.numerator = 30; /* prime number 2 * 3 * 5 */
        frmi->d.denominator = frmi->d.numerator * 10000000 / le32_to_cpu(vsc->dwFrameInterval);

        return 0;
    }

    return -ENODEV;
}

static int usb_video_get_control(USBDevice *dev, int request, int value,
                                 int index, int length, uint8_t *data)
{
    USBVideoState *s = USB_VIDEO(dev);
    uint8_t req = request & 0xff;
    uint8_t cs = value >> 8;
    uint8_t intfnum = index & 0xff;
    uint8_t unit = index >> 8;
    int ret = USB_RET_STALL;

    switch (intfnum) {
    case IF_CONTROL:
        switch (unit) {
        case 0:
            if (length != 1) {
                break;
            }

            if (cs == VC_VIDEO_POWER_MODE_CONTROL) {
                data[0] = 127; /* 4.2.1.1 Power Mode Control */
                ret = 1;
            } else if (cs == VC_REQUEST_ERROR_CODE_CONTROL) {
                data[0] = s->error; /* 4.2.1.2 Request Error Code Control */
                s->error = 0;
                ret = 1;
            }
            break;

        case PROCESSING_UNIT:
            {
                VideodevControlType t = usb_video_pu_control_type_to_qemu(cs);
                handle_get_control(s->pu_attrs, req, t, length, data, ret);
            }
            break;

        case SELECTOR_UNIT:
        case ENCODING_UNIT:
        default:
            /* TODO XU control support */
            break;
        }
        break;

    case IF_STREAMING:
        switch (cs) {
        case VS_PROBE_CONTROL:
            handle_get_streaming(s, req, vsc, length, data, ret);
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "%s: get streamimg %d not implemented\n",
                          TYPE_USB_VIDEO, cs);
        }

        break;
    }

    // trace_usb_video_get_control(bus->busnr, dev->addr, intfnum, unit, cs, ret);
    return ret;
}

static int usb_video_set_control(USBDevice *dev, int request, int value,
                                 int index, int length, uint8_t *data)
{
    USBVideoState *s = USB_VIDEO(dev);
    // USBBus *bus = usb_bus_from_device(dev);
    uint8_t req = request & 0xff;
    uint8_t cs = value >> 8;
    uint8_t intfnum = index & 0xff;
    uint8_t unit = index >> 8;
    int ret = USB_RET_STALL;

    switch (intfnum) {
    case IF_CONTROL:
        switch (unit) {
        case PROCESSING_UNIT:
            {
                uint32_t val = 0;
                VideodevControl ctrl;
                VideodevControlType type;
                Error *local_err = NULL;

                type = usb_video_pu_control_type_to_qemu(cs);
                if (type == VideodevControlMax) {
                    break;
                }

                if (length > 4) {
                    break;
                }

                memcpy(&val, data, length);
                val = le32_to_cpu(val);
                ctrl.type = type;
                ctrl.cur = val;
                if (qemu_videodev_set_control(s->video, &ctrl, &local_err)) {
                    error_reportf_err(local_err, "%s: ", TYPE_USB_VIDEO);
                    break;
                }

                memcpy(&s->pu_attrs[type].value[ATTRIBUTE_CUR], data, length);
                ret = length;
                usb_video_queue_control_status(dev, PROCESSING_UNIT, cs,
                                               &val, length);
            }
            break;

        /* TODO XU control support */
        }

        break;

    case IF_STREAMING:
        switch (cs) {
        case VS_PROBE_CONTROL:
        case VS_COMMIT_CONTROL:
            {
                VideoFrameInterval frmi; // todo: why frmi unused?
                if (usb_video_get_frmi_from_vsc(dev, (VideoStreamingControl *)data, &frmi)) {
                    s->error = VC_ERROR_OUT_OF_RANGE;
                    break;
                }

                ret = usb_video_set_vs_control(dev, req, length, data);
            }
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "%s: set streamimg %d not implemented\n",
                          TYPE_USB_VIDEO, cs);
        }

        break;
    }

    // trace_usb_video_set_control(bus->busnr, dev->addr, intfnum, cs, ret);
    return ret;
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
    case ClassInterfaceRequest | GET_CUR:
    case ClassInterfaceRequest | GET_MIN:
    case ClassInterfaceRequest | GET_MAX:
    case ClassInterfaceRequest | GET_RES:
    case ClassInterfaceRequest | GET_LEN:
    case ClassInterfaceRequest | GET_INFO:
    case ClassInterfaceRequest | GET_DEF:
        ret = usb_video_get_control(dev, request, value, index, length, data);
        if (ret < 0) {
            goto error;
        }
        break;
    case ClassInterfaceOutRequest | SET_CUR:
        ret = usb_video_set_control(dev, request, value, index, length, data);
        if (ret < 0) {
            goto error;
        }
        break;
    case ClassInterfaceRequest | GET_CUR_ALL:
    case ClassInterfaceRequest | GET_MIN_ALL:
    case ClassInterfaceRequest | GET_MAX_ALL:
    case ClassInterfaceRequest | GET_RES_ALL:
    case ClassInterfaceRequest | GET_DEF_ALL:
    case ClassInterfaceOutRequest | SET_CUR_ALL:
    default:
        qemu_log_mask(LOG_UNIMP, "%s: request %d not implemented\n",
                      TYPE_USB_VIDEO, request);
        goto error;
    }

    p->actual_length = ret;
    p->status = USB_RET_SUCCESS;

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
