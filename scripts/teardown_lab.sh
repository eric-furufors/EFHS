#!/usr/bin/env bash
set -e

if [ "$EUID" -ne 0 ]; then
  echo "[-] Please run as root (sudo ./scripts/teardown_lab.sh)"
  exit 1
fi

echo "[+] Removing device mapper target..."
dmsetup remove bad-disk 2>/dev/null || true

if [ -f /tmp/bad_disk_loop.dev ]; then
    LOOP_DEV=$(cat /tmp/bad_disk_loop.dev)
    echo "[+] Detaching loop device $LOOP_DEV..."
    losetup -d "$LOOP_DEV" 2>/dev/null || true
    rm -f /tmp/bad_disk_loop.dev
fi

echo "[+] Removing disk image..."
rm -f /tmp/virt_disk.img

echo "[+] Cleanup complete!"
