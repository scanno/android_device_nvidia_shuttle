#!/bin/sh
make ANDROID_ENV=1 ANDROID=1 ATH_LINUXPATH=/home/edu/org2/kernel31/android-tegra-nv-3.1 ATH_CROSS_COMPILE_TYPE=/home/edu/android-ndk-r5b/toolchains/arm-eabi-4.4.0/prebuilt/linux-x86/bin/arm-eabi-
mkdir bin
cp host/.output/tegra-sdio/image/ar6000.ko ar6000.ko bin
