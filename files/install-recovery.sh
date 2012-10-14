#!/system/bin/sh

###
# Added by Scanno.
# I have added some logging to try to have some kind of trace to see when things
# fail... Also added a check to see if there is a EXT partition on the SDCard.
# If there is no EXT partition, then this script is stopped.
###

LOG_INT2EXT=/data/int2ext.log
    if [ -e $LOG_INT2EXT ]; then
    	rm $LOG_INT2EXT;
    fi;
if [ ! -e /dev/block/mmcblk0p2 ]
then
   echo "EXT partition NOT found !!! I am quitting this process....";
   exit 0;
else
   echo "EXT partition found, lets continue :-)";
fi;

###################################
## CronMod INT2EXT+ - 08/24/2012 ##
##  Written by CronicCorey @xda  ##
##           40int2ext           ##
###################################
## Thanks to Mastermind1024 @xda for helping to solve the IMEI and Baseband issues on some devices

## Set SD cache size
#if [ -e /sys/devices/virtual/bdi/179:0/read_ahead_kb ]
#then
#busybox echo "2048" > /sys/devices/virtual/bdi/179:0/read_ahead_kb;
#fi;

## Make /sd-ext directory if needed and unmount /sd-ext if it already mounted
if [ ! -e /sd-ext ]
then
   echo "/sd-ext is has not been found... we try to create it";
   busybox mount -o remount,rw /;
   busybox mkdir /sd-ext;
   busybox mount -o remount,ro /;
else
   echo "/sd-ext has been found... lets unmount it";
   busybox umount /sd-ext;
fi;

## Move /data mount point to /sd-ext
echo "Moving /data mount point to /sd-ext...";
busybox mount --move /data /sd-ext;

## Mount mmcblk0p2 to /data
echo "Mount mmcblk0p2 to /data...";
busybox mount -o noatime,nodiratime,nosuid,nodev /dev/block/mmcblk0p2 /data;
busybox chown 1000:1000 /data;
busybox chmod 771 /data;

## Move existing files
if [ ! -e /data/app ]
then
   echo "Move existing files...";
   busybox mv /sd-ext/* /data;
fi;

## Move /data, /nvram, /property, and /radio back to /sd-ext
if [ ! -e /sd-ext/data ] && [ -e /data/data ]
then
   echo "Move /data/data back to /sd-ext...";
   busybox mv /data/data /sd-ext;
   busybox mkdir /data/data;
fi;

if [ ! -e /sd-ext/nvram ] && [ -e /data/nvram ] 
then
   echo "Moving /data/nvram back to /sd-ext...";
   busybox mv /data/nvram /sd-ext;
   busybox mkdir /data/nvram;
fi;

if [ ! -e /sd-ext/property ] && [ -e /data/property ]
then
   echo "Moving /property back to /sd-ext...";
   busybox mv /data/property /sd-ext;
   busybox mkdir /data/property;
fi;

if [ ! -e /sd-ext/radio ] && [ -e /data/radio ]
then
   echo "Moving /data/radio back to /sd-ext...";
   busybox mv /data/radio /sd-ext;
   busybox mkdir /data/radio;
fi;

## Make Binds
if [ -e /data/data ]
then
   echo "Bind /sd-ext/data to /data/data...";
   busybox mount -o bind /sd-ext/data /data/data;
fi;

if [ -e /data/nvram ]
then
   echo "Bind /sd-ext/nvram to /data/nvram...";
   busybox mount -o bind /sd-ext/nvram /data/nvram;
fi;

if [ -e /data/property ]
then
   echo "Bind /sd-ext/property to /data/property...";
   busybox mount -o bind /sd-ext/property /data/property;
fi;

if [ -e /data/radio ]
then
   echo "Bind /sd-ext/radio to /data/radio...";
   busybox mount -o bind /sd-ext/radio /data/radio;
fi;

## Unmount /sd-ext
echo "Unmount /sd-ext...";
busybox umount /sd-ext;

sync;
echo "Done with INT2EXT...";

############################################################################################################################################################

########################################################
##              Bind Cache by CyanogenMod             ##
## bind mount /data/local/download to /cache/download ##
##           if cache partition is too small          ##
########################################################

CACHESIZE=$(df -k /cache | tail -n1 | tr -s ' ' | cut -d ' ' -f2)
DATAONLY=$(getprop dalvik.vm.dexopt-data-only)
if [ "$DATAONLY" = "1" ]
then
  NEEDED=60000
else
  NEEDED=105000
fi;

if [ $CACHESIZE -lt $NEEDED ]
then
  mount -o bind /data/local/download /cache/download;
fi;

rm /cache/download/downloadfile*.apk >/dev/null 2>&1;

sync;

############################################################################################################################################################

######################################################################
##                 Automatic ZipAlign by Wes Garner                 ##
## ZipAlign files in /data that have not been previously ZipAligned ##
##                Thanks to oknowton for the changes                ##
######################################################################

LOG_FILE=/data/zipalign.log
    if [ -e $LOG_FILE ]; then
    	rm $LOG_FILE;
    fi;
    	
echo "Starting Automatic ZipAlign" | tee -a $LOG_FILE;
    for apk in /data/app/*.apk ; do
	zipalign -c 4 $apk;
	ZIPCHECK=$?;
	if [ $ZIPCHECK -eq 1 ]; then
		echo ZipAligning $(basename $apk) | tee -a $LOG_FILE;
		zipalign -f 4 $apk /cache/$(basename $apk);
			if [ -e /cache/$(basename $apk) ]; then
				cp -f -p /cache/$(basename $apk) $apk | tee -a $LOG_FILE;
				rm /cache/$(basename $apk);
			else
				echo ZipAligning $(basename $apk) Failed | tee -a $LOG_FILE;
			fi;
	else
		echo ZipAlign already completed on $apk  | tee -a $LOG_FILE;
	fi;
       done;
echo "Automatic ZipAlign finished" | tee -a $LOG_FILE;

