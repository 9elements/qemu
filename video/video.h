#ifndef QEMU_VIDEO_H
#define QEMU_VIDEO_H

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/queue.h"

#define TYPE_VIDEODEV "videodev"
OBJECT_DECLARE_TYPE(Videodev, VideodevClass, VIDEODEV)

struct Videodev {
    Object parent_obj;
};

struct VideodevClass {
    ObjectClass parent_class;

    /* parse command line options and populate QAPI @backend */
    void (*parse)(Videodev *vd, QemuOpts *opts, Error **errp);
    /* called after construction, open/starts the backend */
    void (*open)(Videodev *vd, Error **errp);
};

Videodev *qemu_videodev_new_from_opts(QemuOpts *opts, Error **errp);

#endif /* QEMU_VIDEO_H */
