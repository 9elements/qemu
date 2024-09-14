#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "video/video.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>

#define TYPE_VIDEODEV_V4L2 TYPE_VIDEODEV"-v4l2"

struct V4l2Videodev {
    Videodev parent;

    int fd;

    char* device_path;
};
typedef struct V4l2Videodev V4l2Videodev;

DECLARE_INSTANCE_CHECKER(V4l2Videodev, V4L2_VIDEODEV, TYPE_VIDEODEV_V4L2)

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

    return;

close:
    close(vv->fd);
error:
    g_free(vv);
}

static void video_v4l2_class_init(ObjectClass *oc, void *data)
{
    VideodevClass *vc = VIDEODEV_CLASS(oc);

    vc->parse = video_v4l2_parse;
    vc->open = video_v4l2_open;
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
