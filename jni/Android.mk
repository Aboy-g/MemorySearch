LOCAL_PATH := $(call my-dir)/../

# Keystone
include $(CLEAR_VARS)
LOCAL_MODULE := Keystone
LOCAL_SRC_FILES := core/Keystone/libs-android/$(TARGET_ARCH_ABI)/libkeystone.a
include $(PREBUILT_STATIC_LIBRARY)

# ============================================================
# MemorySearch — 主程序 (交互式控制台)
# ============================================================
include $(CLEAR_VARS)
LOCAL_MODULE := MemorySearch

LOCAL_CPPFLAGS := -std=c++17 -Wall -Wno-error=format-security -fpermissive -fexceptions -Wno-error=c++11-narrowing
LOCAL_CFLAGS   := -Werror=format

# Release 优化
LOCAL_CFLAGS   += -O3 -fvisibility=hidden -fdata-sections -ffunction-sections -DNDEBUG
LOCAL_CPPFLAGS += -O3 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections

# ARM64 优化
ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_CFLAGS   += -march=armv8-a
    LOCAL_CPPFLAGS += -march=armv8-a
endif

LOCAL_LDFLAGS  := -Wl,--gc-sections -Wl,--strip-all -Wl,-exclude-libs,ALL

LOCAL_SRC_FILES := main.cpp \
                core/Mem/Mem.cpp \
                core/Mem/Membase.cpp \
                core/Mem/Process.cpp \
                core/Mem/Search.cpp \
                core/Mem/ProcIO.cpp \
                core/Mem/MemMmap.cpp \
                core/Mem/FuzzySearch.cpp

LOCAL_STATIC_LIBRARIES := Keystone
LOCAL_LDLIBS := -llog -landroid

include $(BUILD_EXECUTABLE)

# ============================================================
# fuzzy_tool — 模糊搜索工具
# ============================================================
include $(CLEAR_VARS)
LOCAL_MODULE := fuzzy_tool

LOCAL_CPPFLAGS := -std=c++17 -O3 -fexceptions -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -DNDEBUG
LOCAL_CFLAGS   := -O3 -fdata-sections -ffunction-sections -DNDEBUG

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_CFLAGS   += -march=armv8-a
    LOCAL_CPPFLAGS += -march=armv8-a
endif

LOCAL_LDFLAGS  := -Wl,--gc-sections -Wl,--strip-all

LOCAL_SRC_FILES := test/fuzzy_tool.cpp \
                core/Mem/Mem.cpp \
                core/Mem/Membase.cpp \
                core/Mem/Process.cpp \
                core/Mem/Search.cpp \
                core/Mem/ProcIO.cpp \
                core/Mem/MemMmap.cpp \
                core/Mem/FuzzySearch.cpp

LOCAL_STATIC_LIBRARIES := Keystone
LOCAL_LDLIBS := -llog -landroid

include $(BUILD_EXECUTABLE)

# ============================================================
# gg_tool — GG 修改器
# ============================================================
include $(CLEAR_VARS)
LOCAL_MODULE := gg_tool

LOCAL_CPPFLAGS := -std=c++17 -O3 -fexceptions -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -DNDEBUG
LOCAL_CFLAGS   := -O3 -fdata-sections -ffunction-sections -DNDEBUG

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_CFLAGS   += -march=armv8-a
    LOCAL_CPPFLAGS += -march=armv8-a
endif

LOCAL_LDFLAGS  := -Wl,--gc-sections -Wl,--strip-all

LOCAL_SRC_FILES := test/gg_tool.cpp \
                core/Mem/Mem.cpp \
                core/Mem/Membase.cpp \
                core/Mem/Process.cpp \
                core/Mem/Search.cpp \
                core/Mem/ProcIO.cpp \
                core/Mem/MemMmap.cpp \
                core/Mem/FuzzySearch.cpp

LOCAL_STATIC_LIBRARIES := Keystone
LOCAL_LDLIBS := -llog -landroid

include $(BUILD_EXECUTABLE)
