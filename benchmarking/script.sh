#!/bin/bash
insmod ../vbd.ko read_latency=20 write_latency=20 nsectors=1048576
mkfs.btrfs  /dev/vbd
mount  /dev/vbd /mnt
mount
cd /mnt
iozone -I -s 10K -r 1K > /home/sanket/vbd/new/btrfs_20_20
cd -
umount /mnt
cat /proc/vbd > /home/sanket/vbd/new/stats_btrfs_20_20
rmmod vbd.ko

insmod ../vbd.ko read_latency=15 write_latency=20  nsectors=1048576
mkfs.btrfs  /dev/vbd
mount  /dev/vbd /mnt
cd /mnt
iozone -I -s 10K -r 1K > /home/sanket/vbd/new/btrfs_15_20
cd -
umount /mnt
cat /proc/vbd > /home/sanket/vbd/new/stats_btrfs_15_20
rmmod vbd.ko

insmod ../vbd.ko read_latency=10 write_latency=20 nsectors=1048576
mkfs.btrfs  /dev/vbd
mount  /dev/vbd /mnt
cd /mnt
iozone -I -s 10K -r 1K > /home/sanket/vbd/new/btrfs_10_20
cd -
umount /mnt
cat /proc/vbd > /home/sanket/vbd/new/stats_btrfs_10_20
rmmod vbd.ko




