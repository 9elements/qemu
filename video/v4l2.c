#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "video/video.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>

#define TYPE_VIDEODEV_V4L2 TYPE_VIDEODEV"-v4l2"

#define V4L2_BUFFER_MAX 16
#define V4L2_BUFFER_DFL 2

typedef struct V4l2Buffer {
    unsigned char *addr;
    uint32_t length;
} V4l2Buffer;

struct V4l2Videodev {
    Videodev parent;
    int fd;
    char* device_path;
    uint8_t nbuffers;
    V4l2Buffer buffers[V4L2_BUFFER_MAX];
};
typedef struct V4l2Videodev V4l2Videodev;

DECLARE_INSTANCE_CHECKER(V4l2Videodev, V4L2_VIDEODEV, TYPE_VIDEODEV_V4L2)

typedef struct VideoV4l2Ctrl {
    VideodevControlType q;
    uint32_t v;
} VideoV4l2Ctrl;

static VideoV4l2Ctrl video_v4l2_ctrl_table[] = {
    { .q = VideodevBrightness,
      .v = V4L2_CID_BRIGHTNESS },
    { .q = VideodevContrast,
      .v = V4L2_CID_CONTRAST },
    { .q = VideodevGain,
      .v = V4L2_CID_GAIN },
    { .q = VideodevGamma,
      .v = V4L2_CID_GAMMA },
    { .q = VideodevHue,
      .v = V4L2_CID_HUE },
    { .q = VideodevHueAuto,
      .v = V4L2_CID_HUE_AUTO },
    { .q = VideodevSaturation,
      .v = V4L2_CID_SATURATION },
    { .q = VideodevSharpness,
      .v = V4L2_CID_SHARPNESS },
    { .q = VideodevWhiteBalanceTemperature,
      .v = V4L2_CID_WHITE_BALANCE_TEMPERATURE },
};

static uint32_t video_qemu_control_to_v4l2(VideodevControlType type)
{
    for (int i = 0; i < ARRAY_SIZE(video_v4l2_ctrl_table); i++) {
        if (video_v4l2_ctrl_table[i].q == type)
            return video_v4l2_ctrl_table[i].v;
    }

    return 0;
}

static void video_v4l2_parse(Videodev *vd, QemuOpts *opts, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    const char *device = qemu_opt_get(opts, "device");
    if (device == NULL) {
        error_setg(errp, QERR_MISSING_PARAMETER, "device");
    }

    vv->device_path = g_strdup(device);
}

static void video_v4l2_open(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    struct stat si;
    struct v4l2_capability v4l2_cap = {0};

    assert(vv->device_path);

    if (-1 == stat(vv->device_path, &si)) {
        error_setg_errno(errp, errno, "cannot identify device %s", vv->device_path);
        goto error;
    }

    if (!S_ISCHR(si.st_mode)) {
        error_setg(errp, "'%s' is no device", vv->device_path);
        goto error;
    }

    vv->fd = open(vv->device_path, O_RDWR | O_NONBLOCK);
    if (vv->fd == -1) {
        error_setg_errno(errp, errno, "cannot open device '%s'", vv->device_path);
        goto error;
    }

    if (-1 == ioctl(vv->fd, VIDIOC_QUERYCAP, &v4l2_cap)) {
        if (errno == EINVAL) {
            error_setg(errp, "device %s is no V4L2 device", vv->device_path);
        } else {
            error_setg_errno(errp, errno, "query device %s failed", vv->device_path);
        }
        goto close;
    }

    if (!(v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(v4l2_cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        error_setg(errp, "%s is not a video capture device", vv->device_path);
        goto close;
    }

    vv->nbuffers = V4L2_BUFFER_DFL;
    return;

close:
    close(vv->fd);
error:
    g_free(vv);
}

static void video_v4l2_close(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    if (close(vv->fd) != 0)
        error_setg_errno(errp, errno, "cannot close device '%s'", vv->device_path);

    g_free(vv->device_path);
    g_free(vv);
}

static void video_v4l2_enum_modes(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    VideoMode *mode;
    VideoFramesize *frmsz;
    VideoFramerate *frmival;
    struct v4l2_fmtdesc v4l2_fmt;
    struct v4l2_frmsizeenum v4l2_frmsz;
    struct v4l2_frmivalenum v4l2_frmival;

    v4l2_fmt.type = V4L2_CAP_VIDEO_CAPTURE;

    for (v4l2_fmt.index = 0; ioctl(vv->fd, VIDIOC_ENUM_FMT, &v4l2_fmt) == 0; v4l2_fmt.index++) {
        if (!qemu_video_pixfmt_supported(v4l2_fmt.pixelformat)) {
            continue;
        }

        vd->nmode++;
        vd->modes = g_realloc(vd->modes, vd->nmode * sizeof(VideoMode));

        mode = &vd->modes[vd->nmode - 1];
        mode->pixelformat = v4l2_fmt.pixelformat;
        mode->framesizes = NULL;
        mode->nframesize = 0;

        v4l2_frmsz.pixel_format = v4l2_fmt.pixelformat;
        for (v4l2_frmsz.index = 0; ioctl(vv->fd, VIDIOC_ENUM_FRAMESIZES, &v4l2_frmsz) == 0; v4l2_frmsz.index++) {
            /* TODO: stepwise support stepwise framesizes*/
            if (v4l2_frmsz.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                continue;
            }

            mode->nframesize++;
            mode->framesizes = g_realloc(mode->framesizes, mode->nframesize * sizeof(VideoFramesize));

            frmsz = &mode->framesizes[mode->nframesize - 1];
            frmsz->width = v4l2_frmsz.discrete.width;
            frmsz->height = v4l2_frmsz.discrete.height;
            frmsz->framerates = NULL;
            frmsz->nframerate = 0;

            v4l2_frmival.pixel_format = mode->pixelformat;
            v4l2_frmival.width = frmsz->width;
            v4l2_frmival.height = frmsz->height;

            for (v4l2_frmival.index = 0; ioctl(vv->fd, VIDIOC_ENUM_FRAMEINTERVALS, &v4l2_frmival) == 0; v4l2_frmival.index++) {
                frmsz->nframerate++;
                frmsz->framerates = g_realloc(frmsz->framerates, frmsz->nframerate * sizeof(VideoFramerate));

                frmival = &frmsz->framerates[frmsz->nframerate - 1];
                frmival->numerator = v4l2_frmival.discrete.numerator;
                frmival->denominator = v4l2_frmival.discrete.denominator;
            }
            if (errno != EINVAL) {
                error_setg_errno(errp, errno, "VIDIOC_ENUM_FRAMEINTERVALS");
            }
        }
        if (errno != EINVAL) {
            error_setg_errno(errp, errno, "VIDIOC_ENUM_FRAMESIZES");
        }
    }
    if (errno != EINVAL) {
        error_setg_errno(errp, errno, "VIDIOC_ENUM_FMT");
    }
}

static int video_v4l2_set_control(Videodev *vd, VideodevControl *ctrl, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    struct v4l2_control v4l2_ctrl;
    uint32_t cid;

    cid = video_qemu_control_to_v4l2(ctrl->type);
    if (!cid) {
        error_setg(errp, "unsupported control type %d", ctrl->type);
        return -EINVAL;
    }

    v4l2_ctrl.id = cid;
    v4l2_ctrl.value = ctrl->cur;

    if (ioctl(vv->fd, VIDIOC_S_CTRL, &v4l2_ctrl) < 0) {
        error_setg(errp, "VIDIOC_S_CTRL on %s failed: %s", vv->device_path, strerror(errno));
        return -errno;
    }

    return 0;
}

// @private
static int video_v4l2_qbuf(Videodev *vd, const int index) {

    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    struct v4l2_buffer buf = {
        .index  = index,
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .field  = V4L2_FIELD_ANY,
        .memory = V4L2_MEMORY_MMAP
    };

    return ioctl(vv->fd, VIDIOC_QBUF, &buf);
}

// @private
static int video_v4l2_dqbuf(Videodev *vd, int *index) {

    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    int ioctl_status = 0;

    struct v4l2_buffer buf = {
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };

    if ((ioctl_status = ioctl(vv->fd, VIDIOC_DQBUF, &buf)) < 0)
        return ioctl_status;

    *index = buf.index;
    return ioctl_status;
}

// @private
static void video_v4l2_free_buffers(Videodev *vd) {

    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    struct v4l2_requestbuffers v4l2_reqbufs = {
        .count  = 0,
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };

    for (int i = 0; i < vv->nbuffers; i++) {

        int index = 0;
        video_v4l2_dqbuf(vd, &index);
    }

    for (int i = 0; i < vv->nbuffers; i++) {

        V4l2Buffer *current_buf = &vv->buffers[i];

        if (current_buf->addr == NULL)
            continue;

        munmap(current_buf->addr, current_buf->length);

        *current_buf = (V4l2Buffer) { // todo: check if vv.buffers need init at construction
            .addr   = NULL,
            .length = 0
        };
    }

    ioctl(vv->fd, VIDIOC_REQBUFS, &v4l2_reqbufs);
}

// @private
static int video_v4l2_setup_buffers(Videodev *vd, Error **errp) {

    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    struct v4l2_requestbuffers v4l2_reqbufs = {
        .count  = vv->nbuffers,
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };

    if (ioctl(vv->fd, VIDIOC_REQBUFS, &v4l2_reqbufs) < 0) {
        error_setg_errno(errp, errno, "VIDIOC_REQBUFS");
        return -1;
    }

    for (int i = 0; i < vv->nbuffers; i++) {

        struct v4l2_buffer v4l2_buf = {
            .index  = i,
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .length = 0
        };

        if (ioctl(vv->fd, VIDIOC_QUERYBUF, &v4l2_buf) < 0) {
            error_setg_errno(errp, errno, "VIDIOC_QUERYBUF");
            goto video_v4l2_setup_buffers_error;
        }

        if (v4l2_buf.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
            continue;

        void *addr = mmap(NULL, v4l2_buf.length, PROT_READ | PROT_WRITE,
                          MAP_SHARED, vv->fd, v4l2_buf.m.offset);

        if (addr == MAP_FAILED) {
            error_setg_errno(errp, errno, "mmap failure");
            goto video_v4l2_setup_buffers_error;
        }

        if (video_v4l2_qbuf(vd, i) < 0) {
            error_setg_errno(errp, errno, "VIDIOC_QBUF");
            goto video_v4l2_setup_buffers_error;
        }

        vv->buffers[i].addr   = addr;
        vv->buffers[i].length = v4l2_buf.length;
    }

    return 0;

video_v4l2_setup_buffers_error:
    video_v4l2_free_buffers(vd);
    return -1;
}

// @private
static int video_v4l2_set_streaming_param(Videodev *vd, Error **errp) {

    struct v4l2_streamparm   stream_param;
    struct v4l2_captureparm* capture_param;
    V4l2Videodev*            vv = V4L2_VIDEODEV(vd);

    stream_param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    capture_param = &stream_param.parm.capture;
    capture_param->timeperframe.numerator   = vd->selected.frmrt.numerator;
    capture_param->timeperframe.denominator = vd->selected.frmrt.denominator;

    if (ioctl(vv->fd, VIDIOC_S_PARM, &stream_param) < 0) {
        error_setg_errno(errp, errno, "VIDIOC_S_PARM");
        return -1;
    }

    return 0;
}

// @private
static int video_v4l2_set_format(Videodev *vd, Error **errp) {

    struct v4l2_format fmt;
    V4l2Videodev*      vv = V4L2_VIDEODEV(vd);

    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = vd->selected.frmsz->width;
    fmt.fmt.pix.height = vd->selected.frmsz->height;
    fmt.fmt.pix.pixelformat = vd->selected.mode->pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(vv->fd, VIDIOC_S_FMT, &fmt) < 0) {
        error_setg_errno(errp, errno, "VIDIOC_S_FMT");
        return -1;
    }

    if (ioctl(vv->fd, VIDIOC_G_FMT, &fmt) < 0) {
        error_setg_errno(errp, errno, "VIDIOC_G_FMT");
        return -1;
    }

    return 0;
}

static int video_v4l2_stream_on(Videodev *vd, Error **errp) {

    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (video_v4l2_set_format(vd, errp) != 0)
        return -1;

    if (video_v4l2_set_streaming_param(vd, errp) != 0)
        return -1;

    if (video_v4l2_setup_buffers(vd, errp) != 0)
        return -1;

    if (ioctl(vv->fd, VIDIOC_STREAMON, &type) < 0) {

        video_v4l2_free_buffers(vd);
        error_setg_errno(errp, errno, "VIDIOC_STREAMON");
        return -1;
    }

    // todo: capture frames

    return 0;
}

static int video_v4l2_stream_off(Videodev *vd, Error **errp) {

    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int retval = 0;

    if (ioctl(vv->fd, VIDIOC_STREAMOFF, &type) < 0) {
        error_setg_errno(errp, errno, "VIDIOC_STREAMOFF");
        retval = -1;
    }

    video_v4l2_free_buffers(vd);
    return retval;
}

static void video_v4l2_class_init(ObjectClass *oc, void *data)
{
    VideodevClass *vc = VIDEODEV_CLASS(oc);

    vc->parse = video_v4l2_parse;
    vc->open = video_v4l2_open;
    vc->close = video_v4l2_close;
    vc->enum_modes = video_v4l2_enum_modes;
    vc->set_control = video_v4l2_set_control;
    vc->stream_on = video_v4l2_stream_on;
    vc->stream_off = video_v4l2_stream_off;
}

static const TypeInfo video_v4l2_type_info = {
    .name = TYPE_VIDEODEV_V4L2,
    .parent = TYPE_VIDEODEV,
    .instance_size = sizeof(V4l2Videodev),
    .class_init = video_v4l2_class_init,
};

static void register_types(void)
{
    type_register_static(&video_v4l2_type_info);
}

type_init(register_types);
