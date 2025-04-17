system_ss.add([files(
  'video.c',
)])

video_modules = {}
foreach m : [
  ['gstreamer', gstreamer, files('gstreamer.c')],
  ['gstreamer-app', gstreamer_app, files('gstreamer.c')],
  ['v4l2', v4l2, files('v4l2.c')],
]
  if m[1].found()
    module_ss = ss.source_set()
    module_ss.add(m[1], m[2])
    video_modules += {m[0] : module_ss}
  endif
endforeach

modules += {'video': video_modules}
