# Device Tree for Samsung Galaxy S Duos (GT-S7562 / kylessopen)

CyanogenMod 9.1 (Android 4.0.4 Ice Cream Sandwich, IMM76L) device configuration for the **Samsung Galaxy S Duos (GT-S7562)**.

---

## Device Specifications

| Feature | Specification |
|:---:|:---|
| **SoC** | Qualcomm MSM7227A Snapdragon S1 |
| **CPU** | 1.0 GHz Cortex-A5 |
| **GPU** | Adreno 200 |
| **RAM** | 768 MB (usable ~645 MB) |
| **Storage** | 4 GB eMMC (expandable via MicroSD up to 32 GB) |
| **Display** | 4.0" 480x800 TFT LCD (233 ppi) |
| **Rear Camera** | 5.0 MP auto-focus with LED flash |
| **Front Camera** | 0.3 MP VGA |
| **Wi-Fi** | Qualcomm Atheros AR6003 (ath6kl) 802.11b/g/n |
| **Bluetooth** | CSR8311 Bluetooth 3.0 + HS |
| **Cellular** | Dual SIM Dual Standby (GSM / HSDPA 7.2 Mbps) |

---

## Status & Features

- **Wi-Fi:** Working (802.11b/g/n, WPA2-PSK, Open networks, solved Status 18 reject & 4-way handshake bugs)
- **Bluetooth:** Working (Clean BlueZ 4.98 stack, CSR `bccmd` init)
- **Camera:** Working (5MP rear camera + VGA front camera, preview orientation & aspect ratio fixed)
- **Audio:** Working (Speaker, earpiece, microphone, hardware OMX decoders)
- **2D / 3D Graphics:** Working (Adreno 200 hardware acceleration & copybit compositing)
- **RIL / Telephony:** Working (Dual-SIM calls, SMS, mobile data)
- **Storage:** Working (Internal storage, external MicroSD, MTP transfer)
- **GPS / Sensors:** Working (Sensors, accelerometer, compass)
- **Alternative App Store:** Pre-bundled patched Google Play Store (3.4.7) pointing to `market.lyano.ovh`

---

## Building from Source

### 1. Initialize CyanogenMod 9.1 repository
```bash
mkdir -p ~/android/cm9 && cd ~/android/cm9
repo init -u git://github.com/CyanogenMod/android.git -b ics
```

### 2. Add local manifest
Create `.repo/local_manifests/kylessopen.xml` (or `.repo/local_manifest.xml`):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <project path="device/samsung/kylessopen" name="qcoretechgh/android_device_samsung_kylessopen" remote="github" revision="cm-9.1" />
  <project path="vendor/samsung/kylessopen" name="qcoretechgh/android_vendor_samsung_kylessopen" remote="github" revision="cm-9.1" />
  <project path="kernel/samsung/kylessopen" name="qcoretechgh/android_kernel_samsung_kylessopen" remote="github" revision="cm-9.1" />
</manifest>
```

Sync the source code:
```bash
repo sync -j8
```

### 3. Apply required core patches
The device tree comes with required patches for `external/wpa_supplicant_8`, `hardware/libhardware_legacy`, `system/netd`, `system/bluetooth`, and `system/core`:

```bash
cd device/samsung/kylessopen/patches
./apply.sh
cd ../../../..
```

### 4. Build the ROM
```bash
. build/envsetup.sh
lunch cm_kylessopen-userdebug
make -j$(nproc) bacon
```

The flashable zip file will be located at:
`out/target/product/kylessopen/cm-9-YYYYMMDD-UNOFFICIAL-kylessopen.zip`

---

## Downloads

* **Prebuilt Flashable ROM (FINAL):** [MediaFire Download](https://www.mediafire.com/file/b1xs8kug7ay973v/cm-9-GT-S7562-FINAL.zip/file)

---

## Credits & Contributors

* **CyanogenMod Team**
* **jonypx09** (initial kylessopen sources)
* **m4jonez**, **KlinkOnE**, **kengnatural**, **v-superuser**
* **pavlepiramida**
