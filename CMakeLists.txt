cmake_minimum_required(VERSION 3.10)
project(TrayIconApp)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加UTF-8编码支持
if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    add_definitions(-D_UNICODE -DUNICODE)
endif()

# 添加可执行文件
add_executable(TrayIconApp WIN32
    src/main.cpp
)

# 设置Windows子系统
set_target_properties(TrayIconApp PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# 复制icon.ico到输出目录
add_custom_command(TARGET TrayIconApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/icon.ico"
        "${CMAKE_BINARY_DIR}/resources/icon.ico"
)

# 链接必要的Windows库
target_link_libraries(TrayIconApp
    shell32
    comdlg32
) 