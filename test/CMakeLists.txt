cmake_minimum_required(VERSION 3.31)
project(test)

set(CMAKE_CXX_STANDARD 17)

add_executable(test
        test1.cpp
)

add_executable(test2
        test2.cpp
)

add_executable(benchmark
        benchmark.cpp
)

add_executable(fuzzy_tool
        fuzzy_tool.cpp
)

add_executable(mmap_bench
        mmap_bench.cpp
)

# 链接所需的库
target_link_libraries(test
        PRIVATE
        Mem
)

target_link_libraries(test2
        PRIVATE
        Mem
)

target_link_libraries(benchmark
        PRIVATE
        Mem
)

target_link_libraries(fuzzy_tool
        PRIVATE
        Mem
)

add_executable(gg_tool
        gg_tool.cpp
)

target_link_libraries(gg_tool
        PRIVATE
        Mem
)

target_link_libraries(mmap_bench
        PRIVATE
        Mem
)