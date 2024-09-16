#ifndef QEMU_VIDEO_H
#define QEMU_VIDEO_H

#include "qemu/osdep.h"
#include "hw/qdev-properties-system.h"
#include "qom/object.h"
#include "qemu/queue.h"

#define TYPE_VIDEODEV "videodev"
OBJECT_DECLARE_TYPE(Videodev, VideodevClass, VIDEODEV)

struct Videodev {
    Object parent_obj;

    char *id;
    bool registered;

    QLIST_ENTRY(Videodev) list;
};

struct VideodevClass {
    ObjectClass parent_class;

    /* parse command line options and populate QAPI @backend */
    void (*parse)(Videodev *vd, QemuOpts *opts, Error **errp);
    /* called after construction, open/starts the backend */
    void (*open)(Videodev *vd, Error **errp);
};

Videodev *qemu_videodev_new_from_opts(QemuOpts *opts, Error **errp);

char *qemu_videodev_get_id(Videodev *vd);
Videodev *qemu_videodev_by_id(char *id, Error **errp);
void qemu_videodev_register(Videodev *vd, Error **errp);

#define DEFINE_VIDEO_PROPERTIES(_s, _f)         \
    DEFINE_PROP_VIDEODEV("videodev", _s, _f)

#endif /* QEMU_VIDEO_H */
