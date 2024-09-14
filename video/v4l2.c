#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "video/video.h"

#define TYPE_VIDEODEV_V4L2 TYPE_VIDEODEV"-v4l2"

struct V4l2Videodev {
    Videodev parent;

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

static void video_v4l2_class_init(ObjectClass *oc, void *data)
{
    VideodevClass *vc = VIDEODEV_CLASS(oc);

    vc->parse = video_v4l2_parse;
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
