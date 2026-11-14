ifeq ($(TARGET_BOOTLOADER_BOARD_NAME),kylessopen)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libcamera
LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false

LOCAL_CFLAGS := -fno-short-enums
LOCAL_CFLAGS += -DQCOM_HARDWARE

ifeq ($(BOARD_CAMERA_USE_GETBUFFERINFO),true)
LOCAL_CFLAGS += -DUSE_GETBUFFERINFO
endif

ifeq ($(BOARD_USE_CAF_LIBCAMERA_GB_REL),true)
LOCAL_CFLAGS += -DCAF_CAMERA_GB_REL
endif

LOCAL_C_INCLUDES := \
    $(TOP)/frameworks/base/include \
    $(TOP)/hardware/libhardware/include \
    $(TOP)/external/jpeg

LOCAL_SRC_FILES := \
    QualcommCameraHardware.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libui \
    libbinder \
    libcutils \
    libcamera_client \
    libjpeg \
    libdl

include $(BUILD_SHARED_LIBRARY)

endif
