#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "video/video.h"

#define TYPE_VIDEODEV_GSTREAMER TYPE_VIDEODEV"-gstreamer"

struct GStreamerVideodev {
    Videodev parent;

    char* pipeline;
};
typedef struct GStreamerVideodev GStreamerVideodev;

DECLARE_INSTANCE_CHECKER(GStreamerVideodev, GSTREAMER_VIDEODEV, TYPE_VIDEODEV_GSTREAMER)

static void video_gstreamer_parse(Videodev *vd, QemuOpts *opts, Error **errp)
{
    GStreamerVideodev *vv = GSTREAMER_VIDEODEV(vd);
    const char *pipeline = qemu_opt_get(opts, "pipeline");
    if (pipeline == NULL) {
        error_setg(errp, QERR_MISSING_PARAMETER, "pipeline");
    }

    vv->pipeline = g_strdup(pipeline);
}

static void video_gstreamer_class_init(ObjectClass *oc, void *data)
{
    VideodevClass *vc = VIDEODEV_CLASS(oc);

    vc->parse = video_gstreamer_parse;
}

static const TypeInfo video_v4l2_type_info = {
    .name = TYPE_VIDEODEV_GSTREAMER,
    .parent = TYPE_VIDEODEV,
    .instance_size = sizeof(GStreamerVideodev),
    .class_init = video_gstreamer_class_init,
};

static void register_types(void)
{
    type_register_static(&video_v4l2_type_info);
}

type_init(register_types);
