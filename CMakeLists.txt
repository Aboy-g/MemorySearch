cmake_minimum_required(VERSION 3.31)
project(MemorySearch)

set(CMAKE_CXX_STANDARD 17)


# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "构建类型" FORCE)
endif()

# 定义所有可用的构建类型
set(CMAKE_CONFIGURATION_TYPES "Release;Debug;RelWithDebInfo;MinSizeRel" CACHE STRING "可用构建类型" FORCE)

message(STATUS "当前构建类型: ${CMAKE_BUILD_TYPE}")

# ========== 通用编译标志 ==========
# 所有构建类型都使用的标志
set(COMMON_CXX_FLAGS "-Wall -Wno-error=format-security -fpermissive -fexceptions -Wno-error=c++11-narrowing")
set(COMMON_C_FLAGS "-Werror=format")

# ========== 根据不同构建类型设置参数 ==========

# 1. Release模式 - 最佳性能
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "配置: Release模式 - 优化性能")

    # 编译标志
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_CXX_FLAGS} -O3 -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_C_FLAGS} -O3 -fdata-sections -ffunction-sections")

    # 链接标志
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--strip-all -Wl,-exclude-libs,ALL")

    # 预处理器定义
    add_definitions(-DNDEBUG)

    message(STATUS "优化级别: O3 (最大性能)")
    message(STATUS "符号处理: 隐藏和剥离")

    # 2. MinSizeRel模式 - 最小体积
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    message(STATUS "配置: MinSizeRel模式 - 最小体积")

    # 编译标志
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_CXX_FLAGS} -Os -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_C_FLAGS} -Os -fdata-sections -ffunction-sections")

    # 链接标志
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--strip-all -Wl,-s -Wl,-exclude-libs,ALL")

    # 预处理器定义
    add_definitions(-DNDEBUG)

    # 额外体积优化（如果代码支持）
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

    message(STATUS "优化级别: Os (最小体积)")
    message(STATUS "符号处理: 完全剥离")

    # 3. Debug模式 - 调试
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "配置: Debug模式 - 调试")

    # 编译标志
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_CXX_FLAGS} -O0 -g3 -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_C_FLAGS} -O0 -g3")

    # 链接标志
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

    # 预处理器定义
    add_definitions(-DDEBUG -D_DEBUG)

    message(STATUS "优化级别: O0 (不优化)")
    message(STATUS "调试信息: 完整")

    # 4. RelWithDebInfo模式 - 带调试的发布版
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "配置: RelWithDebInfo模式 - 带调试的发布版")

    # 编译标志
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_CXX_FLAGS} -O2 -g -fdata-sections -ffunction-sections")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_C_FLAGS} -O2 -g -fdata-sections -ffunction-sections")

    # 链接标志
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections")

    # 预处理器定义
    add_definitions(-DNDEBUG)

    message(STATUS "优化级别: O2 (平衡优化)")
    message(STATUS "调试信息: 保留基本调试信息")

else()
    message(FATAL_ERROR "未知的构建类型: ${CMAKE_BUILD_TYPE}")
endif()

# ========== 架构特定优化 ==========
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a")
    message(STATUS "架构优化: ARM64-v8a")
endif()


add_subdirectory(core)
add_subdirectory(test)

add_executable(${CMAKE_PROJECT_NAME}
    main.cpp
)



# 链接所需的库
target_link_libraries(${CMAKE_PROJECT_NAME}
        PRIVATE
        log
        android
        Mem
)
