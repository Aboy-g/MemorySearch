LOCAL_PATH := $(call my-dir)/../

# Keystone
include $(CLEAR_VARS)
LOCAL_MODULE := Keystone
LOCAL_SRC_FILES := core/Keystone/libs-android/$(TARGET_ARCH_ABI)/libkeystone.a
include $(PREBUILT_STATIC_LIBRARY)


include $(CLEAR_VARS)
# Release 模式编译标志
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wno-error=format-security -fpermissive -fexceptions -Wno-error=c++11-narrowing
LOCAL_CFLAGS   := -Werror=format

# 优化与符号处理
LOCAL_CFLAGS   += -O3 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -DNDEBUG
LOCAL_CPPFLAGS += -O3 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections

# 架构特定优化（arm64-v8a）
ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_CFLAGS   += -march=armv8-a
    LOCAL_CPPFLAGS += -march=armv8-a
endif


LOCAL_CPPFLAGS := -std=c++17 -Wall -Wno-error=format-security -fpermissive -fexceptions -Wno-error=c++11-narrowing -O3 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -DNDEBUG
LOCAL_CFLAGS   := -Werror=format -O3 -fdata-sections -ffunction-sections -DNDEBUG
LOCAL_LDFLAGS  := -Wl,--gc-sections -Wl,--strip-all -Wl,-exclude-libs,ALL

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_CFLAGS   += -march=armv8-a
    LOCAL_CPPFLAGS += -march=armv8-a
endif

LOCAL_MODULE := MemorySearch
LOCAL_SRC_FILES := main.cpp \
                core/Mem/Mem.cpp \
                core/Mem/Search.cpp \
                core/Mem/MemBase.cpp \
                core/Mem/Process.cpp


LOCAL_STATIC_LIBRARIES := Keystone
LOCAL_LDLIBS := -llog -landroid

include $(BUILD_EXECUTABLE)