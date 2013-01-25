# Inherit device configuration for Advent Vega.
$(call inherit-product, device/nvidia/shuttle/full_shuttle.mk)

# Inherit some common cyanogenmod stuff.
$(call inherit-product, vendor/cm/config/common_full_tablet_wifionly.mk)

#
# Setup device specific product configuration.
#
PRODUCT_NAME := cm_shuttle
PRODUCT_BRAND := shuttle
PRODUCT_DEVICE := shuttle
PRODUCT_MODEL := Advent Vega
PRODUCT_MANUFACTURER := shuttle
PRODUCT_BUILD_PROP_OVERRIDES += PRODUCT_NAME=EeePad BUILD_FINGERPRINT=asus/WW_epad/EeePad:4.0.3/IML74K/WW_epad-9.4.3.30-20120604:user/release-keys PRIVATE_BUILD_DESC="WW_epad-user 4.0.3 IML74K WW_epad-9.4.3.30-20120604 release-keys"

# Release name and versioning
PRODUCT_RELEASE_NAME := shuttle

#$(call inherit-product, device/nvidia/shuttle/google_apps.mk)




