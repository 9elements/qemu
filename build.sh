cd build
../configure --target-list=arm-softmmu --disable-docs --disable-fuse \
    --disable-alsa --disable-dsound --disable-gtk \
    --disable-jack --disable-opengl --disable-oss --disable-pipewire \
    --disable-png --disable-pvg --disable-qed --disable-sdl --disable-sdl-image \
    --disable-smartcard --disable-sndio --disable-spice --disable-spice-protocol
make -j$(nproc)