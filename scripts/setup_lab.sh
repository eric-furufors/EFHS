#!/usr/bin/env bash
set -e

if [ "$EUID" -ne 0 ]; then
  echo "[-] Please run as root (sudo ./scripts/setup_lab.sh)"
  exit 1
fi

echo "[+] Creating 100MB virtual disk image..."
dd if=/dev/zero of=/tmp/virt_disk.img bs=1M count=100 status=none

echo "[+] Attaching to loop device..."
LOOP_DEV=$(losetup -f --show /tmp/virt_disk.img)
echo "$LOOP_DEV" > /tmp/bad_disk_loop.dev

echo "[+] Injecting 500ms latency via Device Mapper (dm-delay)..."
SECTORS=$(blockdev --getsz "$LOOP_DEV")
dmsetup create bad-disk --table "0 $SECTORS delay $LOOP_DEV 0 500"

echo "[+] Success! Virtual disk created at /dev/mapper/bad-disk"
