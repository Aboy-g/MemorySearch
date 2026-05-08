cmake_minimum_required(VERSION 3.31)
project(Mem)

set(CMAKE_CXX_STANDARD 17)

file(GLOB MEM_SOURCES "Mem/*.cpp")
add_library(Mem STATIC 
        ${MEM_SOURCES}
)

#导入Keystone静态库
add_library(keystone STATIC IMPORTED)

set(KEYSTONE_LIB_PATH
        ${CMAKE_CURRENT_SOURCE_DIR}/Keystone/libs-android/${ANDROID_ABI}/libkeystone.a
)

set_target_properties(keystone PROPERTIES
        IMPORTED_LOCATION ${KEYSTONE_LIB_PATH}
)

# 链接所需的库
target_link_libraries(Mem
        PRIVATE
        log
        android
        PUBLIC
        keystone
)