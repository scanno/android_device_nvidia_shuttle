#This file adds prebuilt apks to system. Follow the example to add more apks.

LOCAL_PATH := $(call my-dir)

#ShuttleTools
include $(CLEAR_VARS)

LOCAL_MODULE := ShuttleTools
LOCAL_SRC_FILES := $(LOCAL_MODULE).apk
LOCAL_MODULE_CLASS := APPS
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
LOCAL_MODULE_TAGS := optional

include $(BUILD_PREBUILT)

#recovery-reboot.apk
include $(CLEAR_VARS)

LOCAL_MODULE := recovery-reboot
LOCAL_SRC_FILES := $(LOCAL_MODULE).apk
LOCAL_MODULE_CLASS := APPS
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
LOCAL_MODULE_TAGS := optional

include $(BUILD_PREBUILT)
