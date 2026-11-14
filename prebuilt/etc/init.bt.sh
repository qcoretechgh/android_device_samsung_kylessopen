#!/system/bin/sh

LOG_TAG="CSR7820"
BTUART_PORT=/dev/ttyHS0
BTSTATE=/sys/class/rfkill/rfkill0/state
PSCONFIG=/system/etc/PSConfig_7820_rev26.psr

logi ()
{
  /system/bin/log -t $LOG_TAG -p i "$@"
}

loge ()
{
  /system/bin/log -t $LOG_TAG -p e "$@"
}

logi "Initializing CSR7820 Bluetooth..."

# Ensure rfkill power is enabled
if [ -e $BTSTATE ]; then
    echo "1" > $BTSTATE
fi

# Set serial port clock if present
if [ -e /sys/devices/platform/msm_serial_hs.0/clock ]; then
    echo "1" > /sys/devices/platform/msm_serial_hs.0/clock
fi

# Allow serial port to stabilize
usleep 100000

# Download PSConfig and warm reset via bccmd at 115200 baud
logi "Loading PSConfig via bccmd..."
/system/bin/bccmd -t bcsp -d $BTUART_PORT -b 115200 psload -r $PSCONFIG
BCCMD_STATUS=$?

if [ $BCCMD_STATUS -ne 0 ]; then
    loge "bccmd failed with error $BCCMD_STATUS"
    exit $BCCMD_STATUS
fi

logi "bccmd succeeded, waiting for warm reset..."
sleep 1

# Execute hciattach in foreground (-n) so init maintains the PID
logi "Starting hciattach on $BTUART_PORT at 3000000 flow..."
exec /system/bin/hciattach -n -p $BTUART_PORT bcsp 3000000 flow
