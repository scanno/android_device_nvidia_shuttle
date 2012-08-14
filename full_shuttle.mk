# Copyright (C) 2011 The Android Open Source Project
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

# Camera
PRODUCT_PACKAGES := \
    SpareParts \
    Development \
    Stk \
    Mms

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# This is where we'd set a backup provider if we had one
#$(call inherit-product, device/sample/products/backup_overlay.mk)
# Inherit from ahuttle device
$(call inherit-product, device/nvidia/shuttle/device.mk)

# Set those variables here to overwrite the inherited values.
PRODUCT_NAME := full_shuttle
PRODUCT_DEVICE := shuttle
PRODUCT_BRAND := Android
PRODUCT_MODEL := VegaBean Beta 2
PRODUCT_MANUFACTURER := NVidia
BUILD_DISPLAY := VegaBean Beta 2
PRIVATE_BUILD_DESC := "US_epad-user 4.0.3 IML74K US_epad-9.4.2.21-20120323 release-keys"
BUILD_FINGERPRINT := asus/WW_epad/EeePad:4.0.3/IML74K/WW_epad-9.4.3.29-20120511:user/release-keys

$(call inherit-product, device/nvidia/shuttle/google_apps.mk)

