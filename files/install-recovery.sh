#!/system/bin/sh

mount -t ext4 -o rw /dev/block/mmcblk0p2 /mnt/sdcard2
mount -t aufs -o br:/mnt/sdcard2=rw:/data=ro none /data
chmod 777 /data

