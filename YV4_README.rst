===========
YV4 QEMU README
===========
It should be noted that I am using the OpenBMC qemu fork with the changes that
can be found here. Apparently, upstream qemu does not build on my machine because
of a 'deprecated' warning I get when trying to compile slirp functionality (needed for user network).
I don't get the warning when compiling the OpenBMC fork.
I haven't investigated further, if it matters which qemu we are using, before I got the
deprecated warning I was using upstream qemu which worked just fine for me.


Building
========
./configure --target-list=arm-softmmu --disable-docs
make

Yocto changes
=============
The eeproms are manually instantiated via a startup script on image boot.

Running the image
=============
The resulting yocto image is by default to small to be ran in qemu

truncate -S 128M $image


Caveats
=======

- The chassis frus are being probed multiple times in entity-manager, resulting
  in 18 chassis inventory items

- dts changes
