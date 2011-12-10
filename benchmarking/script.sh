#!/bin/bash
mkfs.ext2 /dev/vbd
mount /dev/vbd /mnt
mount
iozone -a -s 128 /mnt > ext2_12_12
umount /mnt
mkfs.ext3 /dev/vbd
mount /dev/vbd /mnt
mount
iozone -a -s 128 /mnt > ext3_12_12
umount /mnt
mkfs.ext4 /dev/vbd
mount /dev/vbd /mnt
mount
iozone -a -s 128 /mnt > ext4_12_12
umount /mnt
mkfs.xfs /dev/vbd
mount /dev/vbd /mnt
mount
iozone -a -s 128 /mnt > xfs_12_12
umount /mnt
mkfs.btrfs /dev/vbd
mount /dev/vbd /mnt
mount
iozone -a -s 128 /mnt > btrfs_12_12
umount /mnt
mkfs.reiserfs -f /dev/vbd
mount /dev/vbd /mnt
mount
iozone -a -s 128 > reiser_12_12
umount /mnt
cat /proc/vbd > stats_12_12
rmmod vbd.ko
