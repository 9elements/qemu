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
#define QEMU_VIDEO_PIX_FMT_NV12   fourcc_code('N', 'V', '1', '2')
#define QEMU_VIDEO_PIX_FMT_MJPEG  fourcc_code('M', 'J', 'P', 'G')
#define QEMU_VIDEO_PIX_FMT_RGB565 fourcc_code('R', 'G', 'B', 'P')

typedef enum VideodevControlType {
    VideodevBrightness,
    VideodevContrast,
    VideodevGain,
    VideodevGamma,
    VideodevHue,
    VideodevHueAuto,
    VideodevSaturation,
    VideodevSharpness,
    VideodevWhiteBalanceTemperature,
    VideodevControlMax
} VideodevControlType;

static inline bool qemu_video_pixfmt_supported(uint32_t pixfmt)
{
    switch (pixfmt) {
    case QEMU_VIDEO_PIX_FMT_YUYV:
    case QEMU_VIDEO_PIX_FMT_NV12:
    return true;
    }

    return false;
}

typedef struct VideodevControl {
    VideodevControlType type;
    int32_t cur;
    int32_t def;
    int32_t min;
    int32_t max;
    int32_t step;
} VideodevControl;

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

typedef struct VideoStreamOptions {
    uint8_t bFormatIndex;
    uint8_t bFrameIndex;
    uint32_t dwFrameInterval; // cpu, not le32
} VideoStreamOptions;

#define TYPE_VIDEODEV "videodev"
OBJECT_DECLARE_TYPE(Videodev, VideodevClass, VIDEODEV)

struct Videodev {
    Object parent_obj;

    char *id;
    bool registered;

    int nmode;
    VideoMode *modes;

    struct SelectedStreamOptions {
        VideoMode *mode;
        VideoFramesize *frmsz;
        VideoFramerate frmrt;
    } selected;

    QLIST_ENTRY(Videodev) list;
};

struct VideodevClass {
    ObjectClass parent_class;

    /* parse command line options and populate QAPI @backend */
    void (*parse)(Videodev *vd, QemuOpts *opts, Error **errp);
    /* called after construction, open/starts the backend */
    void (*open)(Videodev *vd, Error **errp);
    /* called upon deconstruction, closes the backend and frees resources */
    void (*close)(Videodev *vd, Error **errp);
    /* enumerate all supported modes */
    void (*enum_modes)(Videodev *vd, Error **errp);
    /* set control */
    int (*set_control)(Videodev *vd, VideodevControl *ctrl, Error **errp);
    /* start video capture stream */
    int (*stream_on)(Videodev *vd, Error **errp);
    /* stop video capture stream */
    int (*stream_off)(Videodev *vd, Error **errp);
};

/* ====== */

Videodev *qemu_videodev_new_from_opts(QemuOpts *opts, Error **errp);
int qemu_videodev_delete(Videodev *vd, Error **errp);
int qemu_videodev_set_control(Videodev *vd, VideodevControl *ctrl, Error **errp);
bool qemu_videodev_check_options(Videodev *vd, VideoStreamOptions *opts);
int qemu_videodev_stream_on(Videodev *vd, VideoStreamOptions *opts, Error **errp);
int qemu_videodev_stream_off(Videodev *vd, Error **errp);

/* ====== */

char *qemu_videodev_get_id(Videodev *vd);
Videodev *qemu_videodev_by_id(char *id, Error **errp);
void qemu_videodev_register(Videodev *vd, Error **errp);

#define DEFINE_VIDEO_PROPERTIES(_s, _f)         \
    DEFINE_PROP_VIDEODEV("videodev", _s, _f)

#endif /* QEMU_VIDEO_H */
