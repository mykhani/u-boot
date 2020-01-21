#!/bin/sh


if [ $# -ne 1 ]; then
	echo "Usage: $0 <disk>"
	exit 1
fi

DISK=$1

if [ ! -b $DISK ]; then
	echo "Device $DISK not found"
	exit 1
fi

umount ${DEVICE}p*

DISK_SIZE=$(fdisk -l $DISK | grep Disk | grep bytes | awk '{print $5}')

echo "DISK SIZE: $DISK_SIZE"

dd if=/dev/zero of=$DISK seek=0 bs=1024 count=10 conv=notrunc

HEADS=255
SECTORS=63
SECTOR_SIZE=512

CYLINDERS=$(($DISK_SIZE / $HEADS / $SECTORS / $SECTOR_SIZE))

echo "Calculated cylinders: $CYLINDERS"

fdisk $DISK << EOF
o
x
h
$HEADS
s
$SECTORS
c
$CYLINDERS
r
u
n
p
1
1
9
t
b
a
n
p
2
10
$CYLINDERS
w
EOF

mkfs.vfat -F 32 -n "boot" ${DISK}p1
mkfs.ext2 -L "rootfs" ${DISK}p2
