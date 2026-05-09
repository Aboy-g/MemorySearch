cmake_minimum_required(VERSION 3.31)
project(test)

set(CMAKE_CXX_STANDARD 17)

add_executable(test
        test1.cpp
)

add_executable(test2
        test2.cpp
)

# 链接所需的库
target_link_libraries(test
        PRIVATE
        Mem
)

# 链接所需的库
target_link_libraries(test2
        PRIVATE
        Mem
)