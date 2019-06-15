# create a non-skewed std IBM 8" floppy disk image

# start with the system tracks
(cd ../code-z80 && cat boot.com bdos22.com bios.com) >fd.img

# then add plenty of empty markers to cover the directory
printf '\xE5%.0s' {1..2500} >>fd.img

# lastly, increase the file size to 77 tracks of 26 sectors
dd if=/dev/zero of=fd.img bs=128 count=1 seek=2001

ls -l fd.img
