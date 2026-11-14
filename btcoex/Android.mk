LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := btcoex_daemon.c
LOCAL_MODULE := btcoex_daemon
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libcutils liblog
LOCAL_C_INCLUDES := $(LOCAL_PATH)

include $(BUILD_EXECUTABLE)
