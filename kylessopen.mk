# Copyright (C) 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

$(call inherit-product, device/common/gps/gps_eu_supl.mk)

DEVICE_PACKAGE_OVERLAYS := device/samsung/kylessopen/overlay

PRODUCT_NAME := kylessopen
PRODUCT_DEVICE := kylessopen
PRODUCT_MODEL := GT-S7562
PRODUCT_MANUFACTURER := Samsung
PRODUCT_BRAND := samsung

PRODUCT_AAPT_CONFIG := normal hdpi
PRODUCT_AAPT_PREF_CONFIG := hdpi

# Packages
PRODUCT_PACKAGES += \
    make_ext4fs \
    setup_fs \
    dexpreopt \
    e2fsck \
    copybit.kylessopen \
    lights.kylessopen \
    gralloc.kylessopen \
    bdaddr_read \
    toggleshutter \
    libcamera \
    btcoex_daemon

# Prebuilt kernel
LOCAL_KERNEL := device/samsung/kylessopen/kernel
PRODUCT_COPY_FILES += \
    $(LOCAL_KERNEL):kernel

# Vendor blobs
$(call inherit-product-if-exists, vendor/samsung/kylessopen/kylessopen-vendor.mk)

# Ramdisk
PRODUCT_COPY_FILES += \
    device/samsung/kylessopen/init.kylessopen.rc:root/init.kylessopen.rc \
    device/samsung/kylessopen/init.kylessopen.rc:root/init.qcom.rc \
    device/samsung/kylessopen/init.kylessopen.rc:root/init.qct.rc \
    device/samsung/kylessopen/ueventd.kylessopen.rc:root/ueventd.kylessopen.rc \
    device/samsung/kylessopen/ueventd.kylessopen.rc:root/ueventd.qcom.rc \
    device/samsung/kylessopen/ueventd.kylessopen.rc:root/ueventd.qct.rc

# Vold
PRODUCT_COPY_FILES += \
    device/samsung/kylessopen/vold.fstab:system/etc/vold.fstab \
    device/samsung/kylessopen/prebuilt/etc/init.sdcard.sh:system/etc/init.sdcard.sh \
    device/samsung/kylessopen/prebuilt/media_profiles.xml:system/etc/media_profiles.xml

# Key layouts
PRODUCT_COPY_FILES += \
    device/samsung/kylessopen/prebuilt/usr/keylayout/sec_key.kl:system/usr/keylayout/sec_key.kl \
    device/samsung/kylessopen/prebuilt/usr/keylayout/sec_touchscreen.kl:system/usr/keylayout/sec_touchscreen.kl \
    device/samsung/kylessopen/prebuilt/usr/keylayout/sec_powerkey.kl:system/usr/keylayout/sec_powerkey.kl \
    device/samsung/kylessopen/prebuilt/usr/keylayout/sec_jack.kl:system/usr/keylayout/sec_jack.kl \
    device/samsung/kylessopen/prebuilt/usr/keylayout/7x27a_kp.kl:system/usr/keylayout/7x27a_kp.kl \
    device/samsung/kylessopen/prebuilt/usr/keylayout/surf_keypad.kl:system/usr/keylayout/surf_keypad.kl \
    device/samsung/kylessopen/prebuilt/usr/keylayout/7k_handset.kl:system/usr/keylayout/7k_handset.kl

# Audio / Sensor configs
PRODUCT_COPY_FILES += \
    device/samsung/kylessopen/prebuilt/etc/init.d/99performance:system/etc/init.d/99performance \
    device/samsung/kylessopen/prebuilt/etc/AudioFilter.csv:system/etc/AudioFilter.csv \
    device/samsung/kylessopen/prebuilt/etc/AutoVolumeControl.txt:system/etc/AutoVolumeControl.txt \
    device/samsung/kylessopen/prebuilt/etc/sensors.dat:system/etc/sensors.dat \
    device/samsung/kylessopen/prebuilt/etc/calib.dat:system/etc/calib.dat \
    device/samsung/kylessopen/prebuilt/etc/param.dat:system/etc/param.dat \
    device/samsung/kylessopen/prebuilt/etc/PSConfig_7820_rev26.psr:system/etc/PSConfig_7820_rev26.psr \
    device/samsung/kylessopen/prebuilt/etc/init.bt.sh:system/etc/init.bt.sh \
    device/samsung/kylessopen/prebuilt/bin/bccmd:system/bin/bccmd \
    device/samsung/kylessopen/prebuilt/bin/hciattach:system/bin/hciattach \
    device/samsung/kylessopen/prebuilt/bin/btnvtool:system/bin/btnvtool \
    device/samsung/kylessopen/prebuilt/bin/qmuxd:system/bin/qmuxd \
    device/samsung/kylessopen/prebuilt/bin/netmgrd:system/bin/netmgrd \
    device/samsung/kylessopen/prebuilt/bin/sec-ril:system/bin/sec-ril \
    device/samsung/kylessopen/prebuilt/lib/libacdapi_azi.so:system/lib/libacdapi_azi.so \
    device/samsung/kylessopen/prebuilt/lib/hw/sensors.default.so:system/lib/hw/sensors.default.so \
    device/samsung/kylessopen/prebuilt/lib/libril.so:system/lib/libril.so \
    device/samsung/kylessopen/prebuilt/lib/libril-qc-1.so:system/lib/libril-qc-1.so \
    device/samsung/kylessopen/prebuilt/lib/libril-qc-qmi-1.so:system/lib/libril-qc-qmi-1.so \
    device/samsung/kylessopen/prebuilt/lib/libril-qcril-hook-oem.so:system/lib/libril-qcril-hook-oem.so \
    device/samsung/kylessopen/prebuilt/lib/libqmi.so:system/lib/libqmi.so \
    device/samsung/kylessopen/prebuilt/lib/libqmiservices.so:system/lib/libqmiservices.so \
    device/samsung/kylessopen/prebuilt/lib/libcneqmiutils.so:system/lib/libcneqmiutils.so \
    device/samsung/kylessopen/prebuilt/lib/liboncrpc.so:system/lib/liboncrpc.so \
    device/samsung/kylessopen/prebuilt/lib/libauth.so:system/lib/libauth.so \
    device/samsung/kylessopen/prebuilt/lib/libcm.so:system/lib/libcm.so \
    device/samsung/kylessopen/prebuilt/lib/libnv.so:system/lib/libnv.so \
    device/samsung/kylessopen/prebuilt/lib/libpbmlib.so:system/lib/libpbmlib.so \
    device/samsung/kylessopen/prebuilt/lib/libwms.so:system/lib/libwms.so \
    device/samsung/kylessopen/prebuilt/lib/libwmsts.so:system/lib/libwmsts.so \
    device/samsung/kylessopen/prebuilt/lib/libdiag.so:system/lib/libdiag.so \
    device/samsung/kylessopen/prebuilt/lib/libdsutils.so:system/lib/libdsutils.so \
    device/samsung/kylessopen/prebuilt/lib/libdsi_netctrl.so:system/lib/libdsi_netctrl.so \
    device/samsung/kylessopen/prebuilt/lib/libqdp.so:system/lib/libqdp.so \
    device/samsung/kylessopen/prebuilt/lib/libdsm.so:system/lib/libdsm.so \
    device/samsung/kylessopen/prebuilt/lib/libidl.so:system/lib/libidl.so \
    device/samsung/kylessopen/prebuilt/lib/libnetmgr.so:system/lib/libnetmgr.so \
    device/samsung/kylessopen/prebuilt/lib/libqdi.so:system/lib/libqdi.so \
    device/samsung/kylessopen/prebuilt/lib/libqueue.so:system/lib/libqueue.so

# Wi-Fi configs
PRODUCT_COPY_FILES += \
    device/samsung/kylessopen/prebuilt/etc/wifi/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf

# Wi-Fi Kernel modules
PRODUCT_COPY_FILES += \
    device/samsung/kylessopen/prebuilt/system/lib/modules/ath6kl_sdio.ko:system/lib/modules/ath6kl_sdio.ko \
    device/samsung/kylessopen/prebuilt/system/lib/modules/cfg80211.ko:system/lib/modules/cfg80211.ko \
    device/samsung/kylessopen/prebuilt/system/lib/modules/ath.ko:system/lib/modules/ath.ko

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.camera.flash-autofocus.xml:system/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/base/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/base/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml
