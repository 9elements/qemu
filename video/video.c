#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "video/video.h"

static const TypeInfo video_type_info = {
    .name = TYPE_VIDEODEV,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(Videodev),
    .abstract = true,
    .class_size = sizeof(VideodevClass),
};

static void register_types(void)
{
    type_register_static(&video_type_info);
}

type_init(register_types);

typedef struct VideodevClassFE {
    void (*fn)(const char *name, void *opaque);
    void *opaque;
} VideodevClassFE;

static void
videodev_class_foreach(ObjectClass *klass, void *opaque)
{
    VideodevClassFE *fe = opaque;

    assert(g_str_has_prefix(object_class_get_name(klass), "videodev-"));

    fe->fn(object_class_get_name(klass) + sizeof(TYPE_VIDEODEV), fe->opaque);
}

static void
videodev_name_foreach(void (*fn)(const char *name, void *opaque),
                     void *opaque)
{
    VideodevClassFE fe = { .fn = fn, .opaque = opaque };

    object_class_foreach(videodev_class_foreach, TYPE_VIDEODEV, false, &fe);
}

static void
help_string_append(const char *name, void *opaque)
{
    GString *str = opaque;

    g_string_append_printf(str, "\n%s", name);
}

static const VideodevClass *videodev_get_class(const char *backend, Error **errp)
{
    ObjectClass *oc;
    const VideodevClass *vc;
    char *typename = g_strdup_printf("videodev-%s", backend);

    oc = module_object_class_by_name(typename);
    g_free(typename);

    if (!object_class_dynamic_cast(oc, TYPE_VIDEODEV)) {
        error_setg(errp, "'%s' is not a valid videodev backend name", backend);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "backend",
                   "a non-abstract device type");
        return NULL;
    }

    vc = VIDEODEV_CLASS(oc);

    return vc;
}

Videodev *qemu_videodev_new_from_opts(QemuOpts *opts, Error **errp) {
    Error *local_err = NULL;
    Object *obj;
    Videodev *vd;
    const VideodevClass *vc;
    const char *name = qemu_opt_get(opts, "backend");
    const char *id = qemu_opts_id(opts);

    if (name && is_help_option(name)) {
        GString *str = g_string_new("");

        videodev_name_foreach(help_string_append, str);

        qemu_printf("Available videodev backend types: %s\n", str->str);
        g_string_free(str, true);
        return NULL;
    }

    if (id == NULL) {
        error_setg(errp, QERR_MISSING_PARAMETER, "id");
        return NULL;
    }

    if (name == NULL) {
        error_setg(errp, "\"%s\" missing backend", qemu_opts_id(opts));
        return NULL;
    }

    vc = videodev_get_class(name, errp);
    if (vc == NULL) {
        return NULL;
    }

    obj = object_new(object_class_get_name(OBJECT_CLASS(vc)));
    vd = VIDEODEV(obj);

    if (vc->parse) {
        vc->parse(vd, opts, &local_err);
    }
    if (local_err) {
        goto error;
    }

    if (vc->open) {
        vc->open(vd, &local_err);
    }
    if (local_err) {
        goto error;
    }

    return vd;

error:
    error_propagate(errp, local_err);
    return NULL;
}
