#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "qemu/main-loop.h"
#include "video/video.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>

#define TYPE_VIDEODEV_V4L2 TYPE_VIDEODEV"-v4l2"

#define V4L2_BUFFER_MAX 16
#define V4L2_BUFFER_DFL 4

typedef struct V4l2Buffer {
    uint8_t *addr;
    uint32_t length;
} V4l2Buffer;

typedef struct V4l2VideoFrameMetaData {
    int index;
} V4l2VideoFrameMetaData;

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
    VideoControlType q;
    uint32_t v;
} VideoV4l2Ctrl;

static VideoV4l2Ctrl video_v4l2_ctrl_table[] = {
    { .q = VideoControlTypeBrightness,
      .v = V4L2_CID_BRIGHTNESS },
    { .q = VideoControlTypeContrast,
      .v = V4L2_CID_CONTRAST },
    { .q = VideoControlTypeGain,
      .v = V4L2_CID_GAIN },
    { .q = VideoControlTypeGamma,
      .v = V4L2_CID_GAMMA },
    { .q = VideoControlTypeHue,
      .v = V4L2_CID_HUE },
    { .q = VideoControlTypeHueAuto,
      .v = V4L2_CID_HUE_AUTO },
    { .q = VideoControlTypeSaturation,
      .v = V4L2_CID_SATURATION },
    { .q = VideoControlTypeSharpness,
      .v = V4L2_CID_SHARPNESS },
    { .q = VideoControlTypeWhiteBalanceTemperature,
      .v = V4L2_CID_WHITE_BALANCE_TEMPERATURE },
};

static uint32_t video_qemu_control_to_v4l2(VideoControlType type)
{
    for (int i = 0; i < ARRAY_SIZE(video_v4l2_ctrl_table); i++) {

        if (video_v4l2_ctrl_table[i].q == type) {
            return video_v4l2_ctrl_table[i].v;
        }
    }

    return 0;
}

static VideoControlType video_v4l2_control_to_qemu(const uint32_t id)
{
    for (int i = 0; i < ARRAY_SIZE(video_v4l2_ctrl_table); i++) {

        if (video_v4l2_ctrl_table[i].v == id) {
            return video_v4l2_ctrl_table[i].q;
        }
    }

    return VideoControlTypeMax;
}

static int video_v4l2_parse(Videodev *vd, QemuOpts *opts, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    const char *device = qemu_opt_get(opts, "device");

    if (device == NULL) {

        vd_error_setg(vd, errp, QERR_MISSING_PARAMETER, "device");
        return VIDEODEV_RC_ERROR;
    }

    vv->device_path = g_strdup(device);
    return VIDEODEV_RC_OK;
}

static int video_v4l2_open(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    struct v4l2_capability v4l2_cap = { 0 };
    struct stat si;

    assert(vv->device_path);

    if (stat(vv->device_path, &si) == -1) {

        vd_error_setg(vd, errp, "cannot identify device %s", vv->device_path);
        goto error;
    }

    if (!S_ISCHR(si.st_mode)) {

        vd_error_setg(vd, errp, "'%s' is no device", vv->device_path);
        goto error;
    }

    if ((vv->fd = open(vv->device_path, O_RDWR | O_NONBLOCK)) == -1) {

        vd_error_setg(vd, errp, "cannot open device '%s'", vv->device_path);
        goto error;
    }

    if (ioctl(vv->fd, VIDIOC_QUERYCAP, &v4l2_cap) == -1) {

        vd_error_setg(vd, errp, "VIDIOC_QUERYCAP: %s", strerror(errno));
        goto close_and_error;
    }

    if (!(v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(v4l2_cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {

        vd_error_setg(vd, errp, "%s is not a video capture device", vv->device_path);
        goto close_and_error;
    }

    vv->nbuffers = V4L2_BUFFER_DFL;
    return VIDEODEV_RC_OK;

close_and_error:
    close(vv->fd);
error:
    g_free(vv);

    return VIDEODEV_RC_ERROR;
}

static int video_v4l2_close(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    if (close(vv->fd) != 0) {

        vd_error_setg(vd, errp, "cannot close %s", vv->device_path);
        return VIDEODEV_RC_ERROR;
    }

    g_free(vv->device_path);
    g_free(vv);

    return VIDEODEV_RC_OK;
}

static int video_v4l2_enum_modes(Videodev *vd, Error **errp)
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

        vd->nmodes++;
        vd->modes = g_realloc(vd->modes, vd->nmodes * sizeof(VideoMode));

        mode = &vd->modes[vd->nmodes - 1];
        mode->pixelformat = v4l2_fmt.pixelformat;
        mode->framesizes = NULL;
        mode->nframesize = 0;

        v4l2_frmsz.pixel_format = v4l2_fmt.pixelformat;

        for (v4l2_frmsz.index = 0; ioctl(vv->fd, VIDIOC_ENUM_FRAMESIZES, &v4l2_frmsz) == 0; v4l2_frmsz.index++) {

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

                vd_error_setg(vd, errp, "VIDIOC_ENUM_FRAMEINTERVALS: %s", strerror(errno));
                return VIDEODEV_RC_ERROR;
            }
        }

        if (errno != EINVAL) {

            vd_error_setg(vd, errp, "VIDIOC_ENUM_FRAMESIZES: %s", strerror(errno));
            return VIDEODEV_RC_ERROR;
        }
    }

    if (errno != EINVAL) {

        vd_error_setg(vd, errp, "VIDIOC_ENUM_FMT: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_v4l2_enum_controls(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    struct v4l2_queryctrl v4l2_ctrl = { 0 };
    VideoControlType type;

    while (1) {

        v4l2_ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;

        if (ioctl(vv->fd, VIDIOC_QUERYCTRL, &v4l2_ctrl) < 0) {

            if (errno == EINVAL) {
                break;
            }

            vd_error_setg(vd, errp, "VIDIOC_QUERYCTRL: %s", strerror(errno));

            if (vd->controls != NULL)
                g_free(vd->controls);

            return VIDEODEV_RC_ERROR;
        }

        if (v4l2_ctrl.flags & V4L2_CTRL_FLAG_INACTIVE) {
            continue;
        }

        if ((type = video_v4l2_control_to_qemu(v4l2_ctrl.id)) == VideoControlTypeMax) {
            continue;
        }

        vd->ncontrols += 1;
        vd->controls   = g_realloc(vd->controls, vd->ncontrols * sizeof(VideoControl));

        vd->controls[vd->ncontrols - 1] = (VideoControl) {

            .type = type,
            .def  = v4l2_ctrl.default_value,
            .min  = v4l2_ctrl.minimum,
            .max  = v4l2_ctrl.maximum,
            .step = v4l2_ctrl.step
        };
    }

    return VIDEODEV_RC_OK;
}

static int video_v4l2_set_control(Videodev *vd, VideoControl *ctrl, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    struct v4l2_control v4l2_ctrl;
    uint32_t cid;

    if ((cid = video_qemu_control_to_v4l2(ctrl->type)) == 0) {

        vd_error_setg(vd, errp, "unsupported control type %d", ctrl->type);
        return VIDEODEV_RC_INVAL;
    }

    v4l2_ctrl.id    = cid;
    v4l2_ctrl.value = ctrl->cur;

    if (ioctl(vv->fd, VIDIOC_S_CTRL, &v4l2_ctrl) < 0) {

        vd_error_setg(vd, errp, "VIDIOC_S_CTRL: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

// @private
static int video_v4l2_qbuf(Videodev *vd, const int index)
{
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
static int video_v4l2_dqbuf(Videodev *vd, int *index)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    int ioctl_status = 0;

    struct v4l2_buffer buf = {

        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };

    if ((ioctl_status = ioctl(vv->fd, VIDIOC_DQBUF, &buf)) < 0) {
        return ioctl_status;
    }

    *index = buf.index;
    return ioctl_status;
}

// @private
static void video_v4l2_free_buffers(Videodev *vd)
{
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

        if (current_buf->addr == NULL) {
            continue;
        }

        munmap(current_buf->addr, current_buf->length);

        *current_buf = (V4l2Buffer) {

            .addr   = NULL,
            .length = 0
        };
    }

    ioctl(vv->fd, VIDIOC_REQBUFS, &v4l2_reqbufs);
}

// @private
static int video_v4l2_setup_buffers(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    struct v4l2_requestbuffers v4l2_reqbufs = {

        .count  = vv->nbuffers,
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };

    if (ioctl(vv->fd, VIDIOC_REQBUFS, &v4l2_reqbufs) < 0) {

        vd_error_setg(vd, errp, "VIDIOC_REQBUFS: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    for (int i = 0; i < vv->nbuffers; i++) {

        struct v4l2_buffer v4l2_buf = {

            .index  = i,
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .length = 0
        };

        if (ioctl(vv->fd, VIDIOC_QUERYBUF, &v4l2_buf) < 0) {

            vd_error_setg(vd, errp, "VIDIOC_QUERYBUF: %s", strerror(errno));
            goto video_v4l2_setup_buffers_error;
        }

        if (v4l2_buf.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
            continue;
        }

        void *addr = mmap(NULL, v4l2_buf.length, PROT_READ | PROT_WRITE,
                          MAP_SHARED, vv->fd, v4l2_buf.m.offset);

        if (addr == MAP_FAILED) {

            vd_error_setg(vd, errp, "mmap: %s", strerror(errno));
            goto video_v4l2_setup_buffers_error;
        }

        if (video_v4l2_qbuf(vd, i) < 0) {

            vd_error_setg(vd, errp, "VIDIOC_QBUF: %s", strerror(errno));
            goto video_v4l2_setup_buffers_error;
        }

        vv->buffers[i].addr   = addr;
        vv->buffers[i].length = v4l2_buf.length;
    }

    return VIDEODEV_RC_OK;

video_v4l2_setup_buffers_error:
    video_v4l2_free_buffers(vd);
    return VIDEODEV_RC_ERROR;
}

// @private
static int video_v4l2_set_streaming_param(Videodev *vd, Error **errp)
{
    struct v4l2_streamparm   stream_param;
    struct v4l2_captureparm* capture_param;
    V4l2Videodev*            vv = V4L2_VIDEODEV(vd);

    stream_param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    capture_param = &stream_param.parm.capture;
    capture_param->timeperframe.numerator   = vd->selected.frmrt.numerator;
    capture_param->timeperframe.denominator = vd->selected.frmrt.denominator;

    if (ioctl(vv->fd, VIDIOC_S_PARM, &stream_param) < 0) {

        vd_error_setg(vd, errp, "VIDIOC_S_PARM: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

// @private
static int video_v4l2_set_format(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    struct v4l2_format fmt = {

        .type                = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix.width       = vd->selected.frmsz->width,
        .fmt.pix.height      = vd->selected.frmsz->height,
        .fmt.pix.pixelformat = vd->selected.mode->pixelformat,
        .fmt.pix.field       = V4L2_FIELD_NONE
    };

    if (ioctl(vv->fd, VIDIOC_S_FMT, &fmt) < 0) {

        vd_error_setg(vd, errp, "VIDIOC_S_FMT: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    if (ioctl(vv->fd, VIDIOC_G_FMT, &fmt) < 0) {

        vd_error_setg(vd, errp, "VIDIOC_G_FMT: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_v4l2_stream_on(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (video_v4l2_set_format(vd, errp) != VIDEODEV_RC_OK) {
        return VIDEODEV_RC_ERROR;
    }

    if (video_v4l2_set_streaming_param(vd, errp) != VIDEODEV_RC_OK) {
        return VIDEODEV_RC_ERROR;
    }

    if (video_v4l2_setup_buffers(vd, errp) != VIDEODEV_RC_OK) {
        return VIDEODEV_RC_ERROR;
    }

    if (ioctl(vv->fd, VIDIOC_STREAMON, &type) < 0) {

        video_v4l2_free_buffers(vd);
        vd_error_setg(vd, errp, "VIDIOC_STREAMON: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_v4l2_stream_off(Videodev *vd, Error **errp)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(vv->fd, VIDIOC_STREAMOFF, &type) < 0) {

        vd_error_setg(vd, errp, "VIDIOC_STREAMOFF: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    video_v4l2_free_buffers(vd);
    return VIDEODEV_RC_OK;
}
#include "qemu/log.h"
#include "qemu/atomic.h"
#include <sys/syscall.h>
static void video_v4l2_claim_frame(void *opaque)
{
    Videodev *vd = (Videodev*) opaque;
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);

    int index;
    VideoFrame *new_frame;
    V4l2VideoFrameMetaData *meta;

    if (video_v4l2_dqbuf(vd, &index) < 0) {
        return;
    }

    new_frame = g_malloc(sizeof(VideoFrame) + sizeof(*meta));
    new_frame->data       = vv->buffers[index].addr;
    new_frame->bytes_left = vv->buffers[index].length;

    meta = (V4l2VideoFrameMetaData*) new_frame->meta;
    meta->index = index;

    qemu_mutex_lock(&vd->frames.lock);
    QSIMPLEQ_INSERT_TAIL(&vd->frames.queue, new_frame, next);
    qatomic_inc(&vd->frames.total);
    qemu_mutex_unlock(&vd->frames.lock);

    pid_t tid = syscall(SYS_gettid);
    qemu_log("frame enqueued (tid=%d)\n", tid);
}

static int video_v4l2_release_frame(Videodev *vd, Error **errp)
{
    V4l2VideoFrameMetaData *meta = (V4l2VideoFrameMetaData*) vd->current_frame->meta;

    if (video_v4l2_qbuf(vd, meta->index) < 0) {

        vd_error_setg(vd, errp, "VIDIOC_QBUF: %s", strerror(errno));
        return VIDEODEV_RC_ERROR;
    }

    g_free(vd->current_frame);
    vd->current_frame = NULL;

    qemu_log("current_frame released\n");

    return VIDEODEV_RC_OK;
}

static void video_v4l2_install_producer(Videodev *vd)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    qemu_set_fd_handler(vv->fd, video_v4l2_claim_frame, NULL, vd);
}

static void video_v4l2_uninstall_producer(Videodev *vd)
{
    V4l2Videodev *vv = V4L2_VIDEODEV(vd);
    qemu_set_fd_handler(vv->fd, NULL, NULL, NULL);
}

static void video_v4l2_class_init(ObjectClass *oc, void *data)
{
    VideodevClass *vc = VIDEODEV_CLASS(oc);

    vc->parse              = video_v4l2_parse;
    vc->open               = video_v4l2_open;
    vc->close              = video_v4l2_close;
    vc->enum_modes         = video_v4l2_enum_modes;
    vc->enum_controls      = video_v4l2_enum_controls;
    vc->set_control        = video_v4l2_set_control;
    vc->stream_on          = video_v4l2_stream_on;
    vc->stream_off         = video_v4l2_stream_off;
    vc->install_producer   = video_v4l2_install_producer;
    vc->uninstall_producer = video_v4l2_uninstall_producer;
    vc->release_frame      = video_v4l2_release_frame;
}

static const TypeInfo video_v4l2_type_info = {
    .name = TYPE_VIDEODEV_V4L2,
    .parent = TYPE_VIDEODEV,
    .instance_size = sizeof(V4l2Videodev),
    .class_init = video_v4l2_class_init,
};

static void register_types(void) {

    type_register_static(&video_v4l2_type_info);
}

type_init(register_types);
