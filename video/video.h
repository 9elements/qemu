#ifndef QEMU_VIDEO_H
#define QEMU_VIDEO_H

#include "qemu/osdep.h"
#include "hw/qdev-properties-system.h"
#include "qom/object.h"
#include "qemu/queue.h"

#define fourcc_code(a, b, c, d) \
                          ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                          ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define QEMU_VIDEO_PIX_FMT_YUYV   fourcc_code('Y', 'U', 'Y', 'V')

static inline bool qemu_video_pixfmt_supported(uint32_t pixfmt)
{
    switch (pixfmt) {
    case QEMU_VIDEO_PIX_FMT_YUYV:
    return true;
    }

    return false;
}

typedef struct VideoFramerate {
    uint32_t numerator;
    uint32_t denominator;
} VideoFramerate;

typedef struct VideoFramesize {
    uint32_t width;
    uint32_t height;

    int nframerate;
    VideoFramerate *framerates;
} VideoFramesize;

typedef struct VideoModes {
    uint32_t pixelformat;

    int nframesize;
    VideoFramesize *framesizes;
} VideoMode;

#define TYPE_VIDEODEV "videodev"
OBJECT_DECLARE_TYPE(Videodev, VideodevClass, VIDEODEV)

struct Videodev {
    Object parent_obj;

    char *id;
    bool registered;

    int nmode;
    VideoMode *modes;

    QLIST_ENTRY(Videodev) list;
};

struct VideodevClass {
    ObjectClass parent_class;

    /* parse command line options and populate QAPI @backend */
    void (*parse)(Videodev *vd, QemuOpts *opts, Error **errp);
    /* called after construction, open/starts the backend */
    void (*open)(Videodev *vd, Error **errp);
    /* enumerat all supported modes */
    void (*enum_modes)(Videodev *vd, Error **errp);
};

Videodev *qemu_videodev_new_from_opts(QemuOpts *opts, Error **errp);

char *qemu_videodev_get_id(Videodev *vd);
Videodev *qemu_videodev_by_id(char *id, Error **errp);
void qemu_videodev_register(Videodev *vd, Error **errp);

#define DEFINE_VIDEO_PROPERTIES(_s, _f)         \
    DEFINE_PROP_VIDEODEV("videodev", _s, _f)

#endif /* QEMU_VIDEO_H */
