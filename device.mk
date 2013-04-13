#
# Copyright (C) 2011 The Android Open-Source Project
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
#

# This file includes all definitions that apply to ALL shuttle devices, and
# are also specific to shuttle devices
#
# Everything in this directory will become public

LOCAL_PATH := device/nvidia/shuttle

#TARGET_KERNEL_SOURCE := $(LOCAL_PATH)/kernel
#LOCAL_KERNEL := $(LOCAL_PATH)/kernel/zImage

DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay
#PRODUCT_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay/dictionaries

# prefer mdpi drawables where available
PRODUCT_AAPT_CONFIG := normal mdpi hdpi xhdpi
PRODUCT_AAPT_PREF_CONFIG := mdpi

# These are the hardware-specific feature permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/tablet_core_hardware.xml:system/etc/permissions/tablet_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.xml:system/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:system/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:system/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:system/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    packages/wallpapers/LivePicker/android.software.live_wallpaper.xml:system/etc/permissions/android.software.live_wallpaper.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.hardware.location.xml:system/etc/permissions/android.hardware.location.xml
    
#	frameworks/base/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml
#	frameworks/base/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml


# Keychars
# Keylayout
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/files/gpio-keys.kcm:system/usr/keychars/gpio-keys.kcm \
    $(LOCAL_PATH)/files/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl 

# Vold
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/vold.fstab:system/etc/vold.fstab

# Shuttle Configs
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/files/ramdisk/init.rc:root/init.rc \
    $(LOCAL_PATH)/files/ramdisk/init.harmony.rc:root/init.harmony.rc \
    $(LOCAL_PATH)/files/ramdisk/init.harmony.custom.rc:/root/init.harmony.custom.rc \
    $(LOCAL_PATH)/files/ramdisk/init.harmony.usb.rc:root/init.harmony.usb.rc \
    $(LOCAL_PATH)/files/ramdisk/ueventd.harmony.rc:root/ueventd.harmony.rc \
    $(LOCAL_PATH)/files/ramdisk/fstab.shuttle:root/fstab.shuttle

#    $(LOCAL_KERNEL):kernel \
# Backlight
PRODUCT_PACKAGES += \
	lights.shuttle

# HW Composer proxy
PRODUCT_PACKAGES += \
        hwcomposer.tegra
# 3G
PRODUCT_PACKAGES += rild 

# Accelerometer
PRODUCT_PACKAGES += \
	sensors.shuttle 

# Camera
PRODUCT_PACKAGES += \
	camera.shuttle 
	
# GPS
PRODUCT_PACKAGES += \
	gps.shuttle 
	
# Audio
PRODUCT_PACKAGES += \
	audio.primary.shuttle \
	audio.a2dp.default \
        audio.usb.default \
	libaudioutils 
	
# Power
PRODUCT_PACKAGES += \
	power.shuttle

# Touchscreen
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/files/it7260.idc:system/usr/idc/it7260.idc 

# Graphics
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/files/media_profiles.xml:system/etc/media_profiles.xml

# Codecs
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/files/media_codecs.xml:system/etc/media_codecs.xml

# Audio policy configuration
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/files/audio_policy.conf:system/etc/audio_policy.conf

# Huawei 3G modem propietary files and PPP scripts
PRODUCT_PACKAGES += \
     libhuaweigeneric-ril

# Bluetooth
PRODUCT_PACKAGES += \
     libbt-vendor

PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/etc/bluetooth/bluecore6.psr:system/etc/bluetooth/bluecore6.psr \
   $(LOCAL_PATH)/files/etc/bluetooth/bt_vendor.conf:system/etc/bluetooth/bt_vendor.conf

# Dumpstate
PRODUCT_PACKAGES += \
     libdumpstate.shuttle

# Reboot into recovery
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/recovery:system/bin/recovery \
   $(LOCAL_PATH)/files/setrecovery:system/bin/setrecovery 

#PRODUCT_PACKAGES += \
#     setrecovery

PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/etc/init.gprs-pppd:system/etc/init.gprs-pppd \
   $(LOCAL_PATH)/files/etc/ppp/chap-secrets:system/etc/ppp/chap-secrets \
   $(LOCAL_PATH)/files/etc/ppp/gprs-connect-chat:system/etc/ppp/gprs-connect-chat \
   $(LOCAL_PATH)/files/etc/ppp/ip-down:system/etc/ppp/ip-down \
   $(LOCAL_PATH)/files/etc/ppp/ip-down-HUAWEI:system/etc/ppp/ip-down-HUAWEI \
   $(LOCAL_PATH)/files/etc/ppp/ip-up:system/etc/ppp/ip-up \
   $(LOCAL_PATH)/files/etc/ppp/ip-up-HUAWEI:system/etc/ppp/ip-up-HUAWEI \
   $(LOCAL_PATH)/files/etc/ppp/options.huawei:system/etc/ppp/options.huawei \
   $(LOCAL_PATH)/files/etc/ppp/pap-secrets:system/etc/ppp/pap-secrets \
   $(LOCAL_PATH)/files/etc/ppp/peers/pppd-ril.options:system/etc/ppp/peers/gprs \
   $(LOCAL_PATH)/files/etc/ppp/peers/pppd-ril.options:system/etc/ppp/peers/pppd-ril.options

PRODUCT_PROPERTY_OVERRIDES := \
    keyguard.no_require_sim=true

# Generic
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/vold.fstab:system/etc/vold.fstab \
   $(LOCAL_PATH)/files/vega_postboot.sh:system/etc/vega_postboot.sh 
#   $(LOCAL_PATH)/files/flash_image:system/xbin/flash_image 
   
# APNs list
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/apns-conf.xml:system/etc/apns-conf.xml

PRODUCT_PACKAGES += \
	shuttle_hdcp_keys

# NVidia binary blobs
$(call inherit-product, device/nvidia/shuttle/nvidia.mk)
# Modules

# Wifi
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/files/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf \
	$(LOCAL_PATH)/wlan/athtcmd_ram.bin:system/lib/hw/wlan/athtcmd_ram.bin \
	$(LOCAL_PATH)/wlan/athwlan.bin.z77:system/lib/hw/wlan/athwlan.bin.z77 \
	$(LOCAL_PATH)/wlan/data.patch.hw2_0.bin:system/lib/hw/wlan/data.patch.hw2_0.bin \
	$(LOCAL_PATH)/wlan/device.bin:system/lib/hw/wlan/device.bin \
	$(LOCAL_PATH)/wlan/eeprom.bin:system/lib/hw/wlan/eeprom.bin \
	$(LOCAL_PATH)/wlan/eeprom.data:system/lib/hw/wlan/eeprom.data \
	$(LOCAL_PATH)/wlan/abtfilt:system/bin/abtfilt

# 	$(LOCAL_PATH)/wlan/ar6000.ko:system/lib/hw/wlan/ar6000.ko \

PRODUCT_PROPERTY_OVERRIDES := \
    wifi.interface=wlan0 \
    wifi.supplicant_scan_interval=15

#zRAM support
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/zram/lzo_compress.ko:system/lib/modules/lzo_compress.ko \
   $(LOCAL_PATH)/files/zram/lzo_decompress.ko:system/lib/modules/lzo_decompress.ko \
   $(LOCAL_PATH)/files/zram/zram.ko:system/lib/modules/zram.ko \
   $(LOCAL_PATH)/files/zram/zram.sh:system/xbin/zram.sh \
   $(LOCAL_PATH)/files/zram/showtoast.sh:system/xbin/showtoast.sh

#init.d support
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/files/init.d/rc1.d/00banner:system/etc/init.d/rc1.d/00banner \
   $(LOCAL_PATH)/files/init.d/rc1.d/01sysctl:system/etc/init.d/rc1.d/01sysctl \
   $(LOCAL_PATH)/files/init.d/rc1.d/02ril-daemon:system/etc/init.d/rc1.d/02ril-daemon \
   $(LOCAL_PATH)/files/init.d/rc1.d/90systeminit:system/etc/init.d/rc1.d/90systeminit \
   $(LOCAL_PATH)/files/init.d/rc5.d/00banner:system/etc/init.d/rc5.d/00banner \
   $(LOCAL_PATH)/files/init.d/rc5.d/01systweaks:system/etc/init.d/rc5.d/01systweaks \
   $(LOCAL_PATH)/files/init.d/rc5.d/90userinit:system/etc/init.d/rc5.d/90userinit

#   $(LOCAL_PATH)/files/init.d/rc5.d/10zram:system/etc/init.d/rc5.d/10zram \

#USB
PRODUCT_PACKAGES += \
	com.android.future.usb.accessory 

# Set default USB interface
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
	persist.sys.usb.config=mtp

# Live Wallpapers
PRODUCT_PACKAGES += \
	HoloSpiralWallpaper \
        LiveWallpapers \
        LiveWallpapersPicker \
	MagicSmokeWallpapers \
        VisualizationWallpapers

PRODUCT_PROPERTY_OVERRIDES += \
    ro.opengles.version=131072 \
    hwui.render_dirty_regions=false \
    ro.sf.lcd_density=120

PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    ro.secure=0 \
    persist.sys.strictmode.visual=0

ADDITIONAL_DEFAULT_PROPERTIES += ro.secure=0
ADDITIONAL_DEFAULT_PROPERTIES += persist.sys.strictmode.visual=0

PRODUCT_CHARACTERISTICS := tablet

# we have enough storage space to hold precice GC data
PRODUCT_TAGS += dalvik.gc.type-precise

PRODUCT_PACKAGES += \
    librs_jni \
    liba2dp \
    libpkip \
    tinyplay \
    tinycap \
    tinymix \
    wmiconfig

# Filesystem management tools
PRODUCT_PACKAGES += \
	make_ext4fs \
	setup_fs

# Add prebuild apks and superuser
PRODUCT_PACKAGES += \
	CameraGoogle \
	ShuttleTools \
	zRAMconfig

# for bugmailer
#ifneq ($(TARGET_BUILD_VARIANT),user)
#	PRODUCT_PACKAGES += send_bug
#	PRODUCT_COPY_FILES += \
#		system/extras/bugmailer/bugmailer.sh:system/bin/bugmailer.sh \
#		system/extras/bugmailer/send_bug:system/bin/send_bug
#endif

$(call inherit-product, frameworks/native/build/tablet-dalvik-heap.mk)
#$(call inherit-product, vendor/nvidia/shuttle/device-vendor.mk)
