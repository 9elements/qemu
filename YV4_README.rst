===========
YV4 QEMU README
===========

Building
========

Disable sound, smartcard and graphic related dependencies explicitly, as they
seem to compile into the binary by default now.

```
./configure --target-list=arm-softmmu --disable-docs --disable-fuse \
    --disable-alsa --disable-dsound --disable-gtk --disable-gtk-clipboard \
    --disable-jack --disable-opengl --disable-oss --disable-pipewire \
    --disable-png --disable-pvg --disable-qed --disable-sdl --disable-sdl-image \
    --disable-smartcard --disable-sndio --disable-spice --disable-spice-protocol

make
```

Running the image
=============
The resulting yocto image is to small by default to be ran in qemu

truncate -S 128M $image

qemu-system-arm -m 1024 -M fby4-bmc -nographic -drive file=$image,format=raw,if=mtd \
    -net nic -net user,hostfwd=:127.0.0.1:2222-:22,hostfwd=:127.0.0.1:8888-:80,hostfwd=:127.0.0.1:4443-:443,hostfwd=:127.0.0.1:5555-:6666,hostname=qemu
