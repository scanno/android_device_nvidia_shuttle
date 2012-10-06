#!/system/bin/sh

# Configuration area
SHOWTOAST_AM=/system/bin/am
SHOWTOAST_INTENT="android.intent.action.TOAST"
SHOWTOAST_COMPONENT="at.drnet.android.zramconfig/.ZRAMconfigShowToastActivity"
SHOWTOAST_KEY="message"
SHOWTOAST_BUSYBOX=/system/xbin/busybox
SHOWTOAST_ENV_OVERRULE="LD_LIBRARY_PATH=/vendor/lib:/system/lib"
SHOWTOAST_ENV_IFEMPTY=""

# Function area

set_env_var() {
	SHOWTOAST_ENV_NAME=`echo "$1" | $SHOWTOAST_BUSYBOX awk -F '=' '{print $1;}'`
	$SHOWTOAST_BUSYBOX test -z "$SHOWTOAST_ENV_NAME" && return
	SHOWTOAST_ENV_VALUE=`echo "$1" | $SHOWTOAST_BUSYBOX awk -F '=' '{print $2;}'`
	export $SHOWTOAST_ENV_NAME=$SHOWTOAST_ENV_VALUE
}

set_env_var_if_empty() {
	SHOWTOAST_ENV_NAME=`echo "$1" | $SHOWTOAST_BUSYBOX awk -F '=' '{print $1;}'`
	$SHOWTOAST_BUSYBOX test -z "$SHOWTOAST_ENV_NAME" && return
echo Name: $SHOWTOAST_ENV_NAME
	SHOWTOAST_ENV_VALUE=`eval echo \\$$SHOWTOAST_ENV_NAME`
echo Old Value: $SHOWTOAST_ENV_VALUE
	$SHOWTOAST_BUSYBOX test -n "$SHOWTOAST_ENV_VALUE" && return
	SHOWTOAST_ENV_VALUE=`echo "$1" | $SHOWTOAST_BUSYBOX awk -F '=' '{print $2;}'`
echo New Value: $SHOWTOAST_ENV_VALUE
	export $SHOWTOAST_ENV_NAME=$SHOWTOAST_ENV_VALUE
}

# Main entry point

for SHOWTOAST_ENV in $SHOWTOAST_ENV_IFEMPTY; do set_env_var_if_empty "$SHOWTOAST_ENV"; done
for SHOWTOAST_ENV in $SHOWTOAST_ENV_OVERRULE; do set_env_var "$SHOWTOAST_ENV"; done
$SHOWTOAST_AM start -a "$SHOWTOAST_INTENT" -n "$SHOWTOAST_COMPONENT" -e "$SHOWTOAST_KEY" "$1" > /dev/null 2>&1
return $?

