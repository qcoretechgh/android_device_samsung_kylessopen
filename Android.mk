LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_BOOTLOADER_BOARD_NAME),kylessopen)
include $(call all-subdir-makefiles)

# Symlink gralloc.msm7627a.so -> gralloc.msm7x27a.so for SurfaceFlinger
GRALLOC_7627A := $(TARGET_OUT_SHARED_LIBRARIES)/hw/gralloc.msm7627a.so
$(GRALLOC_7627A): $(LOCAL_PATH)/Android.mk
	@echo "Symlink: $@ -> gralloc.msm7x27a.so"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf gralloc.msm7x27a.so $@

ALL_DEFAULT_INSTALLED_MODULES += $(GRALLOC_7627A)

endif
