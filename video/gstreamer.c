#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "video/video.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#define TYPE_VIDEODEV_GSTREAMER TYPE_VIDEODEV"-gstreamer"

struct GStreamerVideodev {
    Videodev parent;

    GstElement *pipeline;
    GstElement *src;
    GstElement *sink;

    struct GStreamerVideoFrame {
        GstSample *sample;
        GstBuffer *buffer;
        GstMapInfo map_info;
    } current_frame;
};
typedef struct GStreamerVideodev GStreamerVideodev;

DECLARE_INSTANCE_CHECKER(GStreamerVideodev, GSTREAMER_VIDEODEV, TYPE_VIDEODEV_GSTREAMER)

typedef struct VideoGStreamerCtrl {
    VideoControlType q;
    const char *v;
} VideoGStreamerCtrl;

static VideoGStreamerCtrl video_gstreamer_ctrl_table[] = {
    {
        .q = VideoControlTypeBrightness,
        .v = "brightness"
    },
    {
        .q = VideoControlTypeContrast,
        .v = "contrast"
    },
    {
        .q = VideoControlTypeHue,
        .v = "hue"
    },
    {
        .q = VideoControlTypeSaturation,
        .v = "saturation"
    }
};

static const char *video_qemu_control_to_gstreamer(VideoControlType type)
{
    for (int i = 0; i < ARRAY_SIZE(video_gstreamer_ctrl_table); i++) {

        if (video_gstreamer_ctrl_table[i].q == type) {
            return video_gstreamer_ctrl_table[i].v;
        }
    }

    return NULL;
}

static int video_gstreamer_parse(Videodev *vd, QemuOpts *opts, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    const char *pipeline_desc = qemu_opt_get(opts, "pipeline");
    GstPad *src_pad;

    if (pipeline_desc == NULL) {
        vd_error_setg(vd, errp, QERR_MISSING_PARAMETER, "pipeline");
        return VIDEODEV_RC_ERROR;
    }

    if (!gst_is_initialized())
        gst_init(NULL, NULL);

    GError *error = NULL;
    gv->pipeline = gst_parse_bin_from_description(pipeline_desc, false, &error);
    if (error) {
        vd_error_setg(vd, errp, "unable to parse pipeline: %s", error->message);
        return VIDEODEV_RC_ERROR;
    }

    src_pad = gst_bin_find_unlinked_pad(GST_BIN(gv->pipeline), GST_PAD_SRC);
    if (!src_pad) {
        vd_error_setg(vd, errp, "pipeline has no unlinked src pad");
        return VIDEODEV_RC_ERROR;
    }

    gv->src = gst_pad_get_parent_element(src_pad);
    gst_object_unref(src_pad);
    if (!gv->src) {
        vd_error_setg(vd, errp, "failed to get pipeline src element");
        return VIDEODEV_RC_ERROR;
    }

    gv->sink = gst_element_factory_make ("appsink", "sink");
    if (!gv->sink) {
        vd_error_setg(vd, errp, "failed to create appsink");
        return VIDEODEV_RC_ERROR;
    }

    gst_bin_add(GST_BIN(gv->pipeline), gv->sink);

    if (!gst_element_link(gv->src, gv->sink)) {
        vd_error_setg(vd, errp, "failed to link pipeline to appsink");
        return VIDEODEV_RC_ERROR;
    }

    gst_element_set_state(gv->pipeline, GST_STATE_READY);
    return VIDEODEV_RC_OK;
}

typedef struct {
    const char *format;
    uint32_t fourcc;
} FormatFourCC;

FormatFourCC formatFourCCMap[] = {
    {"YUY2", QEMU_VIDEO_PIX_FMT_YUYV},
};

static uint32_t gst_format_to_fourcc(const char *format) {
    int i;

    if (!format)
        return 0;

    for (i = 0; i < ARRAY_SIZE(formatFourCCMap); i++) {
        if (!strcmp(formatFourCCMap[i].format, format))
            return formatFourCCMap[i].fourcc;
    }

    return 0;
}

static int video_gstreamer_enum_modes(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstPad *src_pad;
    GstCaps *src_caps;
    const GstStructure *s;
    uint32_t pixelformat;
    VideoMode *mode;
    VideoFramesize *frmsz;
    VideoFramerate *frmival;

    int i, j;
    const gchar *name, *format;
    const GValue *width, *height, *framerates;

    src_pad = gst_element_get_static_pad(gv->src, "src");
    if (!src_pad) {
        vd_error_setg(vd, errp, "failed to get src pad");
        return VIDEODEV_RC_ERROR;
    }

    src_caps = gst_pad_query_caps(src_pad, NULL);
    if (!src_caps) {
        vd_error_setg(vd, errp, "failed to get capabilities from src pad");
        return VIDEODEV_RC_ERROR;
    }

    for (i = 0; i < gst_caps_get_size(src_caps); i++) {
        s = gst_caps_get_structure(src_caps, i);

        name = gst_structure_get_name(s);
        if (strcmp(name, "video/x-raw"))
            continue;

        format = gst_structure_get_string(s, "format");
        if (!format)
            continue;

        pixelformat = gst_format_to_fourcc(format);
        if (pixelformat == 0)
            continue;

        if (!gst_structure_has_field(s, "width"))
            continue;

        width = gst_structure_get_value(s, "width");

        if (GST_VALUE_HOLDS_INT_RANGE(width))
            continue;

        if (!gst_structure_has_field(s, "height"))
            continue;

        height = gst_structure_get_value(s, "height");

        if (GST_VALUE_HOLDS_INT_RANGE(height))
            continue;

        if (!gst_structure_has_field(s, "framerate"))
            continue;

        framerates = gst_structure_get_value(s, "framerate");

        mode = NULL;
        for (j = 0;j < vd->nmode; j++) {
            if (vd->modes[j].pixelformat == pixelformat) {
                mode = &vd->modes[j];
                break;
            }
        }

        if (!mode) {
            vd->nmode++;
            vd->modes = g_realloc(vd->modes, vd->nmode * sizeof(VideoMode));
            mode = &vd->modes[vd->nmode - 1];
            mode->pixelformat = pixelformat;
            mode->framesizes = NULL;
            mode->nframesize = 0;
        }

        mode->nframesize++;
        mode->framesizes = g_realloc(mode->framesizes, mode->nframesize * sizeof(VideoFramesize));
        frmsz = &mode->framesizes[mode->nframesize - 1];

        frmsz->width = g_value_get_int(width);
        frmsz->height = g_value_get_int(height);
        frmsz->framerates = NULL;
        frmsz->nframerate = 0;

        if (GST_VALUE_HOLDS_LIST(framerates)) {
            for (j = 0; j < gst_value_list_get_size(framerates); ++j) {
                const GValue *value = gst_value_list_get_value(framerates, j);

                if (GST_VALUE_HOLDS_FRACTION(value)) {
                    frmsz->nframerate++;
                    frmsz->framerates = g_realloc(frmsz->framerates, frmsz->nframerate * sizeof(VideoFramerate));

                    frmival = &frmsz->framerates[frmsz->nframerate - 1];
                    frmival->numerator = gst_value_get_fraction_numerator(value);
                    frmival->denominator = gst_value_get_fraction_denominator(value);
                }
            }
        } else if (GST_VALUE_HOLDS_FRACTION(framerates)) {
            frmsz->nframerate++;
            frmsz->framerates = g_realloc(frmsz->framerates, frmsz->nframerate * sizeof(VideoFramerate));

            frmival = &frmsz->framerates[frmsz->nframerate - 1];
            frmival->numerator = gst_value_get_fraction_numerator(framerates);
            frmival->denominator = gst_value_get_fraction_denominator(framerates);
        }
    }

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_stream_on(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstStateChangeReturn ret;

    if (gv->pipeline == NULL) {

        vd_error_setg(vd, errp, "GStreamer pipeline not initialized!");
        return VIDEODEV_RC_ERROR;
    }

    ret = gst_element_set_state(gv->pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {

        vd_error_setg(vd, errp, "failed to start GStreamer pipeline!");
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_stream_off(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstStateChangeReturn ret;

    if (gv->pipeline == NULL) {

        vd_error_setg(vd, errp, "GStreamer pipeline not initialized!");
        return VIDEODEV_RC_ERROR;
    }

    ret = gst_element_set_state(gv->pipeline, GST_STATE_READY);

    if (ret == GST_STATE_CHANGE_FAILURE) {

        vd_error_setg(vd, errp, "failed to stop GStreamer pipeline!");
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_claim_frame(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstSample *sample;
    GstBuffer *buffer;

    if ((sample = gst_app_sink_try_pull_sample(GST_APP_SINK(gv->sink), 0)) == NULL) {

        vd_error_setg(vd, errp, "appsink: underrun");
        return VIDEODEV_RC_UNDERRUN;
    }

    if ((buffer = gst_sample_get_buffer(sample)) == NULL) {

        gst_sample_unref(sample);
        vd_error_setg(vd, errp, "could not retrieve sample buffer");
        return VIDEODEV_RC_ERROR;
    }

    if (gst_buffer_map(buffer, &gv->current_frame.map_info, GST_MAP_READ) != TRUE) {

        gst_sample_unref(sample);
        vd_error_setg(vd, errp, "could not map sample buffer");
        return VIDEODEV_RC_ERROR;
    }

    gv->current_frame.sample     = sample;
    gv->current_frame.buffer     = buffer;
    vd->current_frame.data       = (uint8_t*) gv->current_frame.map_info.data;
    vd->current_frame.bytes_left = gv->current_frame.map_info.size;

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_release_frame(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);

    gst_buffer_unmap(gv->current_frame.buffer, &gv->current_frame.map_info);
    gst_sample_unref(gv->current_frame.sample);

    gv->current_frame.sample     = NULL;
    gv->current_frame.buffer     = NULL;
    vd->current_frame.data       = NULL;
    vd->current_frame.bytes_left = 0;

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_set_control(Videodev *vd, VideoControl *ctrl, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    const char *property;
    int value;

    if ((property = video_qemu_control_to_gstreamer(ctrl->type)) == NULL) {

        vd_error_setg(vd, errp, "invalid control property!");
        return VIDEODEV_RC_INVAL;
    }

    g_object_set(G_OBJECT(gv->src), property, ctrl->cur, NULL);
    g_object_get(G_OBJECT(gv->src), property, &value, NULL);

    if (value != ctrl->cur) {

        vd_error_setg(vd, errp, "could not apply new setting for '%s'", property);
        return VIDEODEV_RC_INVAL;
    }

    return VIDEODEV_RC_OK;
}

static void video_gstreamer_class_init(ObjectClass *oc, void *data)
{
    VideodevClass *vc = VIDEODEV_CLASS(oc);

    vc->parse         = video_gstreamer_parse;
    vc->enum_modes    = video_gstreamer_enum_modes;
    vc->stream_on     = video_gstreamer_stream_on;
    vc->stream_off    = video_gstreamer_stream_off;
    vc->claim_frame   = video_gstreamer_claim_frame;
    vc->release_frame = video_gstreamer_release_frame;
    vc->set_control   = video_gstreamer_set_control;
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
