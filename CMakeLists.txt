cmake_minimum_required(VERSION 3.10)

# 项目名称和版本
project(TaskQueue VERSION 1.0)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置编译选项
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Debug模式下的编译选项
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -Wextra -Wpedantic")
# Release模式下的编译选项
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

find_package(Threads REQUIRED)

# 源文件列表（核心库）
set(SOURCES
    event.cpp
    task_queue.cpp
    task_queue_base.cpp
    task_queue_manager.cpp
    task_queue_std.cpp
)

# 头文件列表
set(HEADERS
    event.h
    queued_task.h
    task_queue.h
    task_queue_base.h
    task_queue_manager.h
    task_queue_std.h
)

# 构建静态库，供示例程序复用
add_library(taskqueue STATIC ${SOURCES} ${HEADERS})
target_link_libraries(taskqueue PRIVATE Threads::Threads)

# 示例程序：example/basic（原 example.cpp）
add_executable(example_basic example/example.cpp)
target_link_libraries(example_basic PRIVATE taskqueue Threads::Threads)

# 示例程序：优先级
add_executable(demo_priority example/demo_priority.cpp)
target_link_libraries(demo_priority PRIVATE taskqueue Threads::Threads)

# 示例程序：取消
add_executable(demo_cancel example/demo_cancel.cpp)
target_link_libraries(demo_cancel PRIVATE taskqueue Threads::Threads)

# 示例程序：周期任务
add_executable(demo_periodic example/demo_periodic.cpp)
target_link_libraries(demo_periodic PRIVATE taskqueue Threads::Threads)

# 示例程序：重试任务
add_executable(demo_retry example/demo_retry.cpp)
target_link_libraries(demo_retry PRIVATE taskqueue Threads::Threads)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Debug 相关配置
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # 启用调试信息
    target_compile_definitions(taskqueue PRIVATE DEBUG_MODE)
    # 添加调试符号
    target_compile_options(taskqueue PRIVATE -g)
    # 禁用优化
    target_compile_options(taskqueue PRIVATE -O0)
    # 启用所有警告
    target_compile_options(taskqueue PRIVATE 
        -Wall 
        -Wextra 
        -Wpedantic 
        -Wconversion 
        -Wshadow 
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wcast-qual
        -Wold-style-cast
    )
    # 将警告视为错误
    # target_compile_options(taskqueue PRIVATE -Werror)
endif()

# 添加安装规则（可选）
install(TARGETS taskqueue
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(TARGETS example_basic demo_priority demo_cancel demo_periodic demo_retry
    RUNTIME DESTINATION bin
)

# 安装头文件
install(FILES ${HEADERS} DESTINATION include/${PROJECT_NAME})

# 打印构建类型信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
else()
    message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}") 
endif()