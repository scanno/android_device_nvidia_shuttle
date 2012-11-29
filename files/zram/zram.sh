#!/system/bin/sh

#Create 3 params to write to the logcat. This helps to identify possible
#problems during boot time

LOGI='log -p i -t zRAM'
LOGE='log -p e -t zRAM'
LOGW='log -p w -t zRAM'

#Call: $0 [env|stop|start num size]
# - "$0 env" will give overview of current zRAM environment
# - "$0 start num size [swappiness]" will try to load needed modules and
#   set up "num" zRAM swap devices, with a TOTAL of "size", then set
#   vm.swappiness to "swappiness" if given, else ZRAM_SWAPPINESS_DEFAULT_ON
# - "$0 stop [swappiness]" will stop zRAM swap, try to unload modules, then set
#   vm.swappiness to "swappiness" if given, else ZRAM_SWAPPINESS_DEFAULT_OFF

# Configuration area

# We need a busybox that supports ALL of
# - lsmod
# - insmod with a path
# - rmmod
# - swapon -p
# - sysctl -w
ZRAM_BUSYBOX=/system/xbin/busybox

#Swapiness defaults
# ON defines, how aggressivly we want to use zRAM swap (0..100)
ZRAM_SWAPPINESS_DEFAULT_ON=80
# OFF defines, how aggressivly we want to use swap (0..100), if zRAM is
# disabled. For most use cases (no other swap) this does not matter
# The default for VegaBean is 60 (but doesn't matter)
ZRAM_SWAPPINESS_DEFAULT_OFF=60

# The LOAD variables
# - LOAD contains name and path of the zram kernel module
#   Format: modulename:modulepath
#   modulename is used for lsmod and rmmod, modulepath is used for insmod
#   May be empty, if zram is built into the kernel, but this will allow
#   only exactly one zRAM device, i.e. only "$0 start 1 size" will work,
#   which is perfect for single core, but suboptimal for multicore.
#   Module will be loaded with "num_devices=num"
# - DEP_LOAD contains whitespace-separated list of modules to pre-load
#   entries have same format as LOAD
#   List must contain all dependencies for zRAM, may ofcourse be empty
ZRAM_MODULES_DEP_LOAD="lzo_compress:/system/lib/modules/lzo_compress.ko lzo_decompress:/system/lib/modules/lzo_decompress.ko"
ZRAM_MODULE_LOAD="zram:/system/lib/modules/zram.ko"

# The corresponding UNLOAD variables: Three versions
# - Name(s) or name:path of modules to unload
# - "*" to use reverse of corresponding LOAD variable
# - "" to not unload module(s)
ZRAM_MODULES_DEP_UNLOAD="*"
ZRAM_MODULE_UNLOAD="*"

# These are for the autostart mechanism

# How long minimum time between two autostarts (secs)
ZRAM_MIN_AUTOSTART_INTERVAL=300

# Use this script for toast notifications
ZRAM_SHOWTOAST="/system/xbin/showtoast.sh"

# The path to the data directory and the file names
ZRAM_DATA_DIR="/data/local/zRAM"
ZRAM_DEFAULTS_FILE="defaults"
ZRAM_STARTED_FILE="started"
ZRAM_SUCCESS_FILE="success"

# Constants area
ZRAM_MB=1048576	#1MB in bytes
ZRAM_KB=1024	#1KB in bytes
ZRAM_KB_MB=1024	#1MB in KB

# Functions area

set_env () {
	# 1. Get number of CPUs
	ZRAM_NUM_CPUS=0
	$ZRAM_BUSYBOX test -e /proc/cpuinfo && ZRAM_NUM_CPUS=`$ZRAM_BUSYBOX grep -c BogoMIPS /proc/cpuinfo`

	# 2. Get total memory and swap
	ZRAM_TOTAL_MEM=0
	ZRAM_TOTAL_SWAP=0
	ZRAM_TMP=`$ZRAM_BUSYBOX free | $ZRAM_BUSYBOX grep -iv buffers | $ZRAM_BUSYBOX awk -F ' ' '{print $2;}'`
	for ZRAM_VAL in $ZRAM_TMP; do
		if $ZRAM_BUSYBOX test "$ZRAM_TOTAL_MEM" == 0; then
			ZRAM_TOTAL_MEM=$ZRAM_VAL
		else
			ZRAM_TOTAL_SWAP=$ZRAM_VAL
		fi
	done

	# 3. See if zRAM module is loaded
	ZRAM_LOADED=0
	ZRAM_TMP=`$ZRAM_BUSYBOX lsmod | $ZRAM_BUSYBOX grep -i zram`;
	$ZRAM_BUSYBOX test -n "$ZRAM_TMP" && ZRAM_LOADED=1

	# 4. See which zRAM devices and swap spaces we have
	ZRAM_DEVICES=0
	ZRAM_SIZES=""
	ZRAM_MEM_USED=""
	ZRAM_PAYLOAD=""
	ZRAM_SWAPS=""
	if $ZRAM_BUSYBOX test "$ZRAM_LOADED" == "1"; then
		ZRAM_DEVICES=`ls /dev/block/zram* 2>/dev/null | $ZRAM_BUSYBOX wc -w`
		ZRAM_SIZES=`cat /sys/block/zram*/disksize`
		ZRAM_MEM_USED=`cat /sys/block/zram*/mem_used_total`
		ZRAM_PAYLOAD=`cat /sys/block/zram*/orig_data_size`
		ZRAM_SWAPS=`cat /proc/swaps | $ZRAM_BUSYBOX grep zram | $ZRAM_BUSYBOX awk -F ' ' '{print $1 ":" $3;}'`
	fi
}

load_modules () {
	# Load dependencies (no parameters) i/a
	if $ZRAM_BUSYBOX test -n "$ZRAM_MODULES_DEP_LOAD"; then
		for ZRAM_MOD in $ZRAM_MODULES_DEP_LOAD ; do
			ZRAM_MODULE_NAME=`echo "$ZRAM_MOD" | $ZRAM_BUSYBOX awk -F ':' '{print $1;}'`
			ZRAM_TMP=`$ZRAM_BUSYBOX lsmod | $ZRAM_BUSYBOX grep $ZRAM_MODULE_NAME`
			if $ZRAM_BUSYBOX test -n "$ZRAM_TMP"; then
				echo "LOADING MODULE $ZRAM_MODULE_NAME: already laoded"
				$LOGI "LOADING MODULE $ZRAM_MODULE_NAME: already laoded"
			else
				ZRAM_ERROR="failed"
				ZRAM_MODULE_PATH=`echo "$ZRAM_MOD" | $ZRAM_BUSYBOX awk -F ':' '{print $2;}'`
				$ZRAM_BUSYBOX insmod "$ZRAM_MODULE_PATH" 2>/dev/null && ZRAM_ERROR="success"

				echo "LOADING MODULE '$ZRAM_MODULE_NAME' FROM '$ZRAM_MODULE_PATH': $ZRAM_ERROR"
				$LOGI "LOADING MODULE '$ZRAM_MODULE_NAME' FROM '$ZRAM_MODULE_PATH': $ZRAM_ERROR"

				if $ZRAM_BUSYBOX test "$ZRAM_ERROR" != "success"; then
					return
				fi
			fi
		done
	fi

	# Load zram module i/a
	if $ZRAM_BUSYBOX test -n "$ZRAM_MODULE_LOAD"; then
		ZRAM_MODULE_NAME=`echo "$ZRAM_MODULE_LOAD" | $ZRAM_BUSYBOX awk -F ':' '{print $1;}'`
		ZRAM_MODULE_PATH=`echo "$ZRAM_MODULE_LOAD" | $ZRAM_BUSYBOX awk -F ':' '{print $2;}'`
		ZRAM_TMP=`$ZRAM_BUSYBOX lsmod | $ZRAM_BUSYBOX grep $ZRAM_MODULE_NAME`
		if $ZRAM_BUSYBOX test -n "$ZRAM_TMP"; then
			ZRAM_ERROR="success"
			echo "LOADING MODULE $ZRAM_MODULE_NAME: already laoded"
			$LOGI echo "LOADING MODULE $ZRAM_MODULE_NAME: already laoded"
		else
			ZRAM_ERROR="failed"
			$ZRAM_BUSYBOX insmod "$ZRAM_MODULE_PATH" $@ 2>/dev/null && ZRAM_ERROR="success"
			echo "LOADING MODULE '$ZRAM_MODULE_NAME' WITH PARAMETERS '$@' FROM '$ZRAM_MODULE_PATH': $ZRAM_ERROR"
			$LOGE "LOADING MODULE '$ZRAM_MODULE_NAME' WITH PARAMETERS '$@' FROM '$ZRAM_MODULE_PATH': $ZRAM_ERROR"
		fi
	fi
}

unload_module () {
	$ZRAM_BUSYBOX test "$ZRAM_MODULE_UNLOAD" == "*" && ZRAM_MODULE_UNLOAD="$ZRAM_MODULE_LOAD"
	if $ZRAM_BUSYBOX test -n "$ZRAM_MODULE_UNLOAD"; then
		ZRAM_MODULE_NAME=`echo "$ZRAM_MODULE_UNLOAD" | $ZRAM_BUSYBOX awk -F ':' '{print $1;}'`
		ZRAM_ERROR="failed"
		$ZRAM_BUSYBOX rmmod "$ZRAM_MODULE_NAME" 2>/dev/null && ZRAM_ERROR="success"
		echo "UNLOADING MODULE '$ZRAM_MODULE_NAME': $ZRAM_ERROR"
		$LOGE "UNLOADING MODULE '$ZRAM_MODULE_NAME': $ZRAM_ERROR"
	fi
}

unload_dep_modules () { 
	if $ZRAM_BUSYBOX test "$ZRAM_MODULES_DEP_UNLOAD" == "*"; then
		#reverse list
		ZRAM_MODULES_DEP_UNLOAD="";
		for ZRAM_MOD in "$ZRAM_MODULES_DEP_LOAD" ; do 
			ZRAM_MODULES_DEP_UNLOAD="$ZRAM_MOD $ZRAM_MODULES_DEP_UNLOAD";
		done
	fi
	if $ZRAM_BUSYBOX test -n "$ZRAM_MODULES_DEP_UNLOAD"; then
		#run list
		for ZRAM_MOD in $ZRAM_MODULES_DEP_UNLOAD ; do
			ZRAM_MODULE_NAME=`echo "$ZRAM_MOD" | $ZRAM_BUSYBOX awk -F ':' '{print $1;}'`
			ZRAM_TMP=`$ZRAM_BUSYBOX lsmod | $ZRAM_BUSYBOX grep $ZRAM_MODULE_NAME`
			if $ZRAM_BUSYBOX test -z "$ZRAM_TMP"; then
				echo "UNLOADING MODULE $ZRAM_MODULE_NAME: not loaded"
				$LOGW echo "UNLOADING MODULE $ZRAM_MODULE_NAME: not loaded"
			else
				ZRAM_ERROR="failed"
				$ZRAM_BUSYBOX rmmod "$ZRAM_MODULE_NAME" 2>/dev/null && ZRAM_ERROR="success"
				echo "UNLOADING MODULE '$ZRAM_MODULE_NAME': $ZRAM_ERROR"
				$LOGE "UNLOADING MODULE '$ZRAM_MODULE_NAME': $ZRAM_ERROR"
				if $ZRAM_BUSYBOX test "$ZRAM_ERROR" != "success"; then
					return
				fi
			fi
		done
	fi
}

function isint () {
	ZRAM_TMP=`$ZRAM_BUSYBOX printf "%d" $1 2>/dev/null`
	if $ZRAM_BUSYBOX test "$1" == "$ZRAM_TMP"; then
		echo 1
	else
		echo 0
	fi
}

# Main control flow

# Make sure, we have a command
if $ZRAM_BUSYBOX test -z "$1"; then
	echo "Usage: $0 command [parameters]"
	exit 1
fi

# Enter environment
set_env

# Decide command
case "$1" in
	env)
		echo "** ENV: Start"
		echo [0] Loaded: $ZRAM_LOADED
		echo [1] Devices: $ZRAM_DEVICES
		echo [2] CPUs: $ZRAM_NUM_CPUS
		echo [3] RAM size KB: $ZRAM_TOTAL_MEM
		echo [4] Total swap KB: $ZRAM_TOTAL_SWAP
		echo [5] zRAM swaps KB: $ZRAM_SWAPS
		echo [6] zRAM sizes B: $ZRAM_SIZES
		echo [7] zRAM mem usage B: $ZRAM_MEM_USED
		echo [8] zRAM payloads B: $ZRAM_PAYLOAD
		echo "** ENV: Success"
		;;

	start)
		echo "** START: Start"
		$LOGI "** START: Start"

		# check device count
		if $ZRAM_BUSYBOX test `isint $2` == 1; then
			echo "[0] NUM_DEVICES: $2";
		else
			echo "** START: '$2' Bad number of devices"
			return 1
		fi
		if $ZRAM_BUSYBOX test "$2" -lt 1; then
			echo "** START: Number of devices $2 too low"
			return 1
		fi
		if $ZRAM_BUSYBOX test "$2" -gt 8; then
			echo "** START: Number of devices $2 too high"
			return 1
		fi

		# check size
		ZRAM_TOTAL=$((`echo "$3" | $ZRAM_BUSYBOX sed "s/%/*$ZRAM_TOTAL_MEM\/100/; s/M/*$ZRAM_KB_MB/; s/K//"`))
		if $ZRAM_BUSYBOX test `isint $ZRAM_TOTAL` == 1; then
			echo "[1] TOTAL_SIZE KB: $ZRAM_TOTAL";
		else
			echo "** START: '$ZRAM_TOTAL' Bad total zRAM size"
			return 1
		fi
		if $ZRAM_BUSYBOX test "$ZRAM_TOTAL" -lt $((10*$ZRAM_KB_MB)); then
			echo "** START: Total zRAM size $ZRAM_TOTAL too low"
			return 1
		fi
		if $ZRAM_BUSYBOX test "$ZRAM_TOTAL" -gt $((7*$ZRAM_TOTAL_MEM/10)); then
			echo "** START: Total zRAM size $ZRAM_TOTAL too high"
			return 1
		fi

		# check for default swappiness override
		if $ZRAM_BUSYBOX test `isint $4` != 1; then
			ZRAM_SWAPPINESS=$ZRAM_SWAPPINESS_DEFAULT_ON
			echo "SWAPPINESS: No number given, using default $ZRAM_SWAPPINESS_DEFAULT_ON"
		else
			ZRAM_SWAPPINESS=$4
			echo "SWAPPINESS: $ZRAM_SWAPPINESS given"
			if $ZRAM_BUSYBOX test "$ZRAM_SWAPPINESS" -lt 10; then
				echo "SWAPPINESS: Number too low, using default $ZRAM_SWAPPINESS_DEFAULT_ON"
				ZRAM_SWAPPINESS=$ZRAM_SWAPPINESS_DEFAULT_ON
			fi
			if $ZRAM_BUSYBOX test "$ZRAM_SWAPPINESS" -gt 100; then
				echo "SWAPPINESS: Number too high, using default $ZRAM_SWAPPINESS_DEFAULT_ON"
				ZRAM_SWAPPINESS=$ZRAM_SWAPPINESS_DEFAULT_ON
			fi
		fi

		# load modules
		ZRAM_ERROR="success"
		echo "LOADING MODULES: Start"
		load_modules "num_devices=$2"
		echo "LOADING MODULES: $ZRAM_ERROR"
		if $ZRAM_BUSYBOX test "$ZRAM_ERROR" != "success"; then
			echo "** START: Error loading modules"
			$LOGE "** START: Error loading modules"
			return 1
		fi

		# Check, that requested and actual device count match
		ZRAM_DEVICES=`ls /dev/block/zram* 2>/dev/null | $ZRAM_BUSYBOX wc -w`
		if $ZRAM_BUSYBOX test "$ZRAM_DEVICES" != "$2"; then
			echo "** START: Error setting up $2 zRAM devices (got $ZRAM_DEVICES)"
			$LOGE "** START: Error setting up $2 zRAM devices (got $ZRAM_DEVICES)"
			return 1
		fi

		# Set swappiness
		echo SWAPPINESS: setting to $ZRAM_SWAPPINESS
		$ZRAM_BUSYBOX sysctl -w vm.swappiness=$ZRAM_SWAPPINESS > /dev/null 2>&1

		# Set disksize and initialize swap for devices
		ZRAM_DISKSIZE=$(($ZRAM_TOTAL/$ZRAM_DEVICES))
		ZRAM_ERROR="";
		for ZRAM_TMP in /sys/block/zram*; do
			ZRAM_DEV=`$ZRAM_BUSYBOX basename $ZRAM_TMP`
			echo "INITIALIZING $ZRAM_DEV"
			echo $((ZRAM_KB*ZRAM_DISKSIZE)) > $ZRAM_TMP/disksize || ZRAM_ERROR="disksize"
			if $ZRAM_BUSYBOX test -z "$ZRAM_ERROR"; then
				$ZRAM_BUSYBOX mkswap /dev/block/$ZRAM_DEV $ZRAM_DISKSIZE > /dev/null 2>&1 || ZRAM_ERROR="mkswap"
			fi
			if $ZRAM_BUSYBOX test -z "$ZRAM_ERROR"; then
				$ZRAM_BUSYBOX swapon -p 1 /dev/block/$ZRAM_DEV  > /dev/null 2>&1 || ZRAM_ERROR="swapon"
			fi
			if $ZRAM_BUSYBOX test -n "$ZRAM_ERROR"; then
				echo "** START: Error in stage '$ZRAM_ERROR' for device '$ZRAM_DEV'"
				$LOGE "** START: Error in stage '$ZRAM_ERROR' for device '$ZRAM_DEV'"
				return 1
			fi
		done

		# Set swappiness again
		echo SWAPPINESS: setting to $ZRAM_SWAPPINESS
		$ZRAM_BUSYBOX sysctl -w vm.swappiness=$ZRAM_SWAPPINESS > /dev/null 2>&1

		# Read swappiness
		echo -n "[2] Swappiness: "
		$ZRAM_BUSYBOX sysctl vm.swappiness 2>/dev/null | $ZRAM_BUSYBOX sed 's/vm.swappiness = //'

		#Done!
		echo "** START: Success"
		$LOGI "** START: Success"
		return 0
		;;

	stop)
		echo "** STOP: Start"

		# check for default swappiness override
		if $ZRAM_BUSYBOX test `isint $2` != 1; then
			ZRAM_SWAPPINESS=$ZRAM_SWAPPINESS_DEFAULT_OFF
			echo "SWAPPINESS: No number given, using default $ZRAM_SWAPPINESS_DEFAULT_OFF"
		else
			ZRAM_SWAPPINESS=$2
			echo "SWAPPINESS: $ZRAM_SWAPPINESS given"
			if $ZRAM_BUSYBOX test "$ZRAM_SWAPPINESS" -lt 10; then
				echo "SWAPPINESS: Number too low, using default $ZRAM_SWAPPINESS_DEFAULT_OFF"
				ZRAM_SWAPPINESS=$ZRAM_SWAPPINESS_DEFAULT_OFF
			fi
			if $ZRAM_BUSYBOX test "$ZRAM_SWAPPINESS" -gt 100; then
				echo "SWAPPINESS: Number too high, using default $ZRAM_SWAPPINESS_DEFAULT_OFF"
				ZRAM_SWAPPINESS=$ZRAM_SWAPPINESS_DEFAULT_OFF
			fi
		fi

		# Switch off swaps
		if $ZRAM_BUSYBOX test -z "$ZRAM_SWAPS"; then
			echo "SWAPS: None active"
		else
			# Set swappiness
			echo SWAPPINESS: setting to $ZRAM_SWAPPINESS
			$ZRAM_BUSYBOX sysctl -w vm.swappiness=$ZRAM_SWAPPINESS > /dev/null 2>&1

			# Disable swap
			for ZRAM_SWAP in $ZRAM_SWAPS; do
				ZRAM_DEV=`echo "$ZRAM_SWAP" | $ZRAM_BUSYBOX awk -F ':' '{print $1;}'`
				echo "SWAP: Switching off $ZRAM_DEV"
				$ZRAM_BUSYBOX swapoff $ZRAM_DEV
			done
		fi

		# Set swappiness back again
		echo SWAPPINESS: setting to $ZRAM_SWAPPINESS
		$ZRAM_BUSYBOX sysctl -w vm.swappiness=$ZRAM_SWAPPINESS > /dev/null 2>&1

		# Read swappiness
		echo -n "[0] Swappiness: "
		$ZRAM_BUSYBOX sysctl vm.swappiness 2>/dev/null | $ZRAM_BUSYBOX sed 's/vm.swappiness = //'

		# Reset devices
		if $ZRAM_BUSYBOX test "$ZRAM_DEVICES" == 0; then
			echo "DEVICES: None active"
		else
			for ZRAM_TMP in /sys/block/zram*; do
				echo "RESET: Resetting $ZRAM_TMP"
				echo "1" > $ZRAM_TMP/reset
				echo "0" > $ZRAM_TMP/disksize
				echo "1" > $ZRAM_TMP/reset
			done
		fi

		# Unload zram module
		ZRAM_ERROR="success"
		if $ZRAM_BUSYBOX test "$ZRAM_LOADED" != 1; then
			echo "MODULE: Not loaded"
		else
			echo "MODULE: Unloading"
			unload_module
		fi
		
		# Unload dependency modules, if this worked
		if $ZRAM_BUSYBOX test "$ZRAM_ERROR" == "success"; then
			unload_dep_modules
		fi

		# Done		
		if $ZRAM_BUSYBOX test "$ZRAM_ERROR" != "success"; then
			echo "** STOP: $ZRAM_ERROR"
			return 1
		else
			echo "** STOP: Success"
			return 0
		fi
		;;
	loaddefaults)
		echo "** LOADDEFAULTS: Start"
		$LOGI "** LOADDEFAULTS: Start"

		# Bail out, if defaults file doesn't exist ...
		if $ZRAM_BUSYBOX test -f "$ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"; then
			echo "FILE: defaults found in $ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"
			$LOGI echo "FILE: defaults found in $ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"

			ZRAM_DEFAULTS=`$ZRAM_BUSYBOX cat "$ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"`
			ZRAM_TMP=`echo "$ZRAM_DEFAULTS" | $ZRAM_BUSYBOX wc -w`
			# ... or doesn't have 3 parameters
			if $ZRAM_BUSYBOX test "$ZRAM_TMP" == 3; then
				echo "FILE: defaults file contains 3 parameters (OK)"
				$LOGI "FILE: defaults file contains 3 parameters (OK)"
			else
				echo "FILE: defaults file contains $ZRAM_TMP parameters, 3 expected"
				echo "** LOADDEFAULTS: Defaults file bad"
				$LOGE "** LOADDEFAULTS: Defaults file bad"
				$LOGE "zRAM autostart skipped, bad defaults file"

				$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart skipped, bad defaults file'
				return 1
			fi
		else
			echo "FILE: defaults not found in $ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"
			echo "** LOADDEFAULTS: Defaults file not found"
			$LOGW "FILE: defaults not found in $ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"
			$LOGW "** LOADDEFAULTS: Defaults file not found"
			#Do not show notification - autostart was disabled
			return 1 
		fi

		# Bail out, if success flag file doesn't exist ...
		if $ZRAM_BUSYBOX test -f "$ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"; then
			echo "FILE: success flag found in $ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"
			$LOGI "FILE: success flag found in $ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"

			ZRAM_SUCCESS=`$ZRAM_BUSYBOX cat "$ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"`
			# ... or doesn't have a single number
			if $ZRAM_BUSYBOX test `isint $ZRAM_SUCCESS` != 1; then
				echo "FILE: Success flag didn't contain a single number"
				echo "** LOADDEFAULTS: Success flag file bad"
				$LOGI "FILE: Success flag didn't contain a single number"
				$LOGI "** LOADDEFAULTS: Success flag file bad"

				$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart skipped, aborted try detected'
				return 1
			else 
				# ... or is too young
				ZRAM_TMP=`$ZRAM_BUSYBOX date +%s`
				ZRAM_TMP=$(($ZRAM_TMP-$ZRAM_SUCCESS));
				echo "FILE: Success flag is $ZRAM_TMP seconds old"
				$LOGI "FILE: Success flag is $ZRAM_TMP seconds old"

				if $ZRAM_BUSYBOX test "$ZRAM_TMP" -lt "$ZRAM_MIN_AUTOSTART_INTERVAL"; then
					echo "FILE: Success was younger than minimum of $ZRAM_MIN_AUTOSTART_INTERVAL seconds"
					echo "FILE: Bailing out to avoid potential boot loop"
					echo "** LOADDEFAULTS: Success flag too young"
					$LOGW "FILE: Success was younger than minimum of $ZRAM_MIN_AUTOSTART_INTERVAL seconds"
					$LOGW "FILE: Bailing out to avoid potential boot loop"
					$LOGW "** LOADDEFAULTS: Success flag too young"

					$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart skipped, potential bootloop detected'
					return 1
				fi
			fi
		else
			echo "FILE: success flag not found in $ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"
			echo "** LOADDEFAULTS: Success flag not found"
			$LOGW "FILE: success flag not found in $ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"
			$LOGW "** LOADDEFAULTS: Success flag not found"

			$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart skipped, aborted try detected'
			return 1 
		fi

		# Bail out, if startup flag file exist
		if $ZRAM_BUSYBOX test -f "$ZRAM_DATA_DIR/$ZRAM_STARTED_FILE"; then
			echo "FILE: Stale startup flag found in $ZRAM_DATA_DIR/$ZRAM_STARTED_FILE"
			echo "** LOADDEFAULTS: Stale startup flag found"
			$LOGW "FILE: Stale startup flag found in $ZRAM_DATA_DIR/$ZRAM_STARTED_FILE"
			$LOGW "** LOADDEFAULTS: Stale startup flag found"

			$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart skipped, aborted try detected'
			return 1 
		else
			echo "FILE: No stale startup flag found in $ZRAM_DATA_DIR/$ZRAM_STARTED_FILE (OK)"
			$LOGI "FILE: No stale startup flag found in $ZRAM_DATA_DIR/$ZRAM_STARTED_FILE (OK)"
		fi

		# Bail out, if current device size(s) exist
		if $ZRAM_BUSYBOX test -z "$ZRAM_SIZES"; then
			echo "RUNNING: zRAM not yet configured (OK)"
			$LOGI "RUNNING: zRAM not yet configured (OK)"
		else
			echo "RUNNING: zRAM already configured (has sizes)"
			echo "** LOADDEFAULTS: Already running"
			$LOGW "RUNNING: zRAM already configured (has sizes)"
			$LOGW "** LOADDEFAULTS: Already running"

			$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart skipped, already running'
			return 1 
		fi

		# Create subcommand
		ZRAM_CMD="$0 start"
		for ZRAM_TMP in $ZRAM_DEFAULTS; do
			ZRAM_CMD="$ZRAM_CMD $ZRAM_TMP"
		done

		# delete success flag file, create startup flag file 
		echo "FLAGS: Setting 'started', resetting 'success'"
		$LOGI "FLAGS: Setting 'started', resetting 'success'"
		$ZRAM_BUSYBOX rm "$ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"
		$ZRAM_BUSYBOX date +%s > "$ZRAM_DATA_DIR/$ZRAM_STARTED_FILE"

		echo "SUBSCRIPT: Calling '$ZRAM_CMD'"
		$LOGI "SUBSCRIPT: Calling '$ZRAM_CMD'"
		ZRAM_LASTLINE=`$ZRAM_CMD | $ZRAM_BUSYBOX tail -n 1`
		ZRAM_EXITCODE=$?
		echo "SUBSCRIPT: Returned with last line '$ZRAM_LASTLINE' and exit code $ZRAM_EXITCODE"
		$LOGW "SUBSCRIPT: Returned with last line '$ZRAM_LASTLINE' and exit code $ZRAM_EXITCODE"

		# Bail out, if subsommand failed
		if $ZRAM_BUSYBOX test "$ZRAM_LASTLINE" != "** START: Success"; then
			echo "SUBSCRIPT: Output returned did not indicate success"
			echo "** LOADDEFAULTS: Start script failed with '$ZRAM_LASTLINE'"
			$LOGE "SUBSCRIPT: Output returned did not indicate success"
			$LOGE "** LOADDEFAULTS: Start script failed with '$ZRAM_LASTLINE'"

			$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart failed'
			return 1 
		fi

		#Clean up
		echo "SUBSCRIPT: Output returned indicated success, cleaning up"
		$LOGI "SUBSCRIPT: Output returned indicated success, cleaning up"
		# Write success flag file, delete startup flag file 
		echo "FLAGS: Resetting 'started', setting 'success'"
		$LOGI "FLAGS: Resetting 'started', setting 'success'"

		$ZRAM_BUSYBOX rm "$ZRAM_DATA_DIR/$ZRAM_STARTED_FILE"
		$ZRAM_BUSYBOX date +%s > "$ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"

		# Done
		echo "** LOADDEFAULTS: Success"
		$LOGI "** LOADDEFAULTS: Success"
		$ZRAM_BUSYBOX test -x "$ZRAM_SHOWTOAST" && $ZRAM_SHOWTOAST 'zRAM autostart succeded'
		;;

	setdefaults)
		echo "** SETDEFAULTS: Start"
		$LOGI "** SETDEFAULTS: Start"
		# Make sure, data directory exists
		if $ZRAM_BUSYBOX test -d "$ZRAM_DATA_DIR"; then
			echo "DIR: Data directory exists in '$ZRAM_DATA_DIR'"
			$LOGI echo "DIR: Data directory exists in '$ZRAM_DATA_DIR'"
		else
			# try to create it
			echo "DIR: Data directory does not exis in '$ZRAM_DATA_DIR', trying to create it"
			$LOGI "DIR: Data directory does not exis in '$ZRAM_DATA_DIR', trying to create it"

			$ZRAM_BUSYBOX mkdir -p "$ZRAM_DATA_DIR"
			# we can't rely on the exit code, but need to retest
			if $ZRAM_BUSYBOX test -d "$ZRAM_DATA_DIR"; then
				echo "DIR: Data directory created in '$ZRAM_DATA_DIR'"
				$LOGI "DIR: Data directory created in '$ZRAM_DATA_DIR'"
			else
				echo "DIR: Could not create data directory"
				echo "** SETDEFAULTS: Could not create data directory"
				$LOGW "DIR: Could not create data directory"
				$LOGW "** SETDEFAULTS: Could not create data directory"

				return 1 
			fi
		fi

		# Is zRAM started?
		if $ZRAM_BUSYBOX test -z "$ZRAM_SIZES"; then
			# Not running: Delete defaults file to mark no autostart
			$ZRAM_BUSYBOX rm "$ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE" > /dev/null 2>&1
			echo "RUNNING: zRAM not configured, disabling autostart"
			$LOGI "RUNNING: zRAM not configured, disabling autostart"
		else
			echo "RUNNING: zRAM already configured, enabling autostart"
			$LOGI "RUNNING: zRAM already configured, enabling autostart"
			# Write device count
			echo $ZRAM_DEVICES > "$ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"
			# Calculate and write total size
			ZRAM_TMP=`cat /sys/block/zram0/disksize`
			echo $(($ZRAM_DEVICES*$ZRAM_TMP/1024)) >> "$ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"
			# Get and write swappiness
			$ZRAM_BUSYBOX sysctl vm.swappiness 2>/dev/null | $ZRAM_BUSYBOX sed 's/vm.swappiness = //' >> "$ZRAM_DATA_DIR/$ZRAM_DEFAULTS_FILE"
			echo "FILE: Wrote new defaults file"
			$LOGI "FILE: Wrote new defaults file"
		fi

		# Write success flag file, delete startup flag file 
		echo "FLAGS: Resetting 'started', setting 'success'"
		$LOGI "FLAGS: Resetting 'started', setting 'success'"
		$ZRAM_BUSYBOX rm "$ZRAM_DATA_DIR/$ZRAM_STARTED_FILE" > /dev/null 2>&1
		echo "0" > "$ZRAM_DATA_DIR/$ZRAM_SUCCESS_FILE"

		# Done
		echo "** SETDEFAULTS: Success"
		$LOGI "** SETDEFAULTS: Success"
		;;

	*)
		echo "Unknown command '$1'"
		return 1
		;;
esac

