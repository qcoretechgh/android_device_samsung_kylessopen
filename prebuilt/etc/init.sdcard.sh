#!/system/bin/sh
# Fallback internal storage on /data/media/sdcard.img when no physical microSD is present.

LOG_TAG="sdcard_fallback"
IMG_DIR="/data/media"
IMG_PATH="/data/media/sdcard.img"
LOOP_DEV="/dev/block/loop7"
LOOP_SYS="/sys/devices/virtual/block/loop7"

logi() {
    /system/bin/log -t "$LOG_TAG" -p i "$@"
}

# Wait briefly for mmcblk1 to settle at boot
WAIT=0
while [ $WAIT -lt 6 ]; do
    if [ -e /sys/block/mmcblk1 ] || [ -e /dev/block/mmcblk1 ]; then
        logi "Physical microSD detected, fallback skipped"
        exit 0
    fi
    usleep 500000
    WAIT=$((WAIT + 1))
done

# Do not interfere if /mnt/sdcard is already mounted
if grep -q " /mnt/sdcard " /proc/mounts; then
    logi "/mnt/sdcard already mounted, fallback skipped"
    exit 0
fi

# Ensure /data is mounted before creating/attaching image
if ! grep -q " /data " /proc/mounts; then
    logi "/data not mounted, aborting fallback"
    exit 1
fi

if [ ! -d "$IMG_DIR" ]; then
    mkdir -p "$IMG_DIR"
    chmod 0755 "$IMG_DIR"
fi

NEED_MKFS=0
if [ ! -f "$IMG_PATH" ]; then
    logi "Creating 384MB sparse FAT32 fallback image at $IMG_PATH"
    dd if=/dev/zero of="$IMG_PATH" bs=1048576 seek=383 count=1 >/dev/null 2>&1
    chmod 0640 "$IMG_PATH"
    NEED_MKFS=1
fi

# Attach loop7 if not already attached
CUR_SIZE=0
if [ -r "$LOOP_SYS/size" ]; then
    CUR_SIZE=$(cat "$LOOP_SYS/size")
fi

if [ "$CUR_SIZE" -eq 0 ] 2>/dev/null; then
    /system/xbin/losetup "$LOOP_DEV" "$IMG_PATH"
    if [ $? -ne 0 ]; then
        logi "losetup $LOOP_DEV failed"
        exit 1
    fi
fi

if [ "$NEED_MKFS" -eq 1 ]; then
    /system/bin/newfs_msdos -F 32 -L INTERNAL "$LOOP_DEV" >/dev/null 2>&1
fi

# Trigger vold to recognize loop7 and notify MountService
if [ -w "$LOOP_SYS/uevent" ]; then
    echo add > "$LOOP_SYS/uevent"
fi

logi "Internal SD fallback active on $LOOP_DEV ($IMG_PATH)"
exit 0
