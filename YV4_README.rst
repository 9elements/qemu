===========
YV4 QEMU README
===========
It should be noted that I am using the OpenBMC qemu fork with the changes that
can be found here. Apparently, upstream qemu does not build on my machine because
of a 'deprecated' warning I get when trying to compile slirp functionality (needed for user network).
I don't get the warning when compiling the OpenBMC fork.
I haven't investigated further if it matters which qemu we are using, before I got the
deprecated warning I was using upstream qemu which worked just fine for me.

Building
========
./configure --target-list=arm-softmmu --disable-docs
make

Yocto changes
=============
https://github.com/9elements/openbmc branch: yosemite4

Running the image
=============
The resulting yocto image is by default to small to be ran in qemu

truncate -S 128M $image

qemu-system-arm -m 1024 -M fby35-bmc -nographic -drive file=$image,format=raw,if=mtd \
    -net nic -net user,hostfwd=:127.0.0.1:2222-:22,hostfwd=:127.0.0.1:8888-:80,hostfwd=:127.0.0.1:4443-:443,hostfwd=:127.0.0.1:5555-:6666,hostname=qemu

Caveats
=======

- The chassis frus are being probed multiple times in entity-manager, resulting
  in 16 blade chassis inventory items

- dts eeprom entries are not picked up correctly, hence currently i am istantiating them manually
