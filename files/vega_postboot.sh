#!/system/bin/sh
L="log -p i -t postboot"
$L "vega_postboot.sh started"

#######################################################################
# Assign BT a proper unique mac address based off the wlan mac address# 
#######################################################################
if [ -e /data/bluez_provider/bluez_reg ] && [ -e /data/bluez_provider/mac_reg ] ; then
$L "bluez registered"
else
if [ -e /sys/devices/platform/sdhci-tegra.0/mmc_host/mmc0/mmc0:0001/mmc0:0001:1/net/wlan0/address ]; then
mkdir /data/bluez_provider/
ADDR=`cat /sys/devices/platform/sdhci-tegra.0/mmc_host/mmc0/mmc0:0001/mmc0:0001:1/net/wlan0/address`
echo $ADDR
A=`echo $ADDR | cut -d ':' -f 4`
B=`echo $ADDR | cut -d ':' -f 5`
C=`echo $ADDR | cut -d ':' -f 6`
D=`echo $ADDR | cut -d ':' -f 3`
E=`echo $ADDR | cut -d ':' -f 1`
F=`echo $ADDR | cut -d ':' -f 2`
line1='// BT_ADDR'
line2="&0001 = 00$A $C$B 00$D $E$F"
echo $line1 > /data/bluez_provider/bluecore6.psr
echo $line2 >> /data/bluez_provider/bluecore6.psr
cat /system/etc/bluecore6.psr >> /data/bluez_provider/bluecore6.psr
echo "done" > /data/bluez_provider/bluez_reg
echo "done" > /data/bluez_provider/mac_reg
chmod 777 /data/bluez_provider/bluecore6.psr
cat /data/bluez_provider/bluecore6.psr > /system/etc/bluez/bluecore6.psr 
else
mkdir /data/bluez_provider/
cat /system/etc/bluez/bluecore6.psr > /data/bluez_provider/bluecore6.psr
chmod 777 /data/bluez_provider/bluecore6.psr
  fi
fi

##########################################################################
# Misc Filesystem permissions
chmod -R 755 /system/bin
chmod 6755 /system/bin/su
chmod 6755 /system/xbin/su
chmod -R 755 /system/etc
chown 1010 /system/etc/wifi
chgrp 1010 /system/etc/wifi
chmod 777 /system/etc/wifi
chmod 644 /system/etc/wifi/wpa_supplicant.conf
chown wifi:wifi /system/etc/wifi/wpa_supplicant.conf 
chmod 777 /data/misc/wifi
touch /data/misc/wifi/ipconfig.txt
chmod 777 /data/misc/wifi/wpa_supplicant.conf
chmod 6755 /system/bin/pppd
chown radio:system /system/etc/ppp/ip-up
#########################################################################
#Misc tunings


