# create a disk image of 32 MB (fuzix fs) + 4 MB (swap)

cp -a rootfs-z80-4.img hd.img
dd if=/dev/zero of=hd.img bs=512 count=8192 seek=65536

ls -l hd.img
