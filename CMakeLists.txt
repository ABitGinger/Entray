cmake_minimum_required(VERSION 3.20)

project(Entray
    VERSION 2.0.0
    DESCRIPTION "在系统托盘里创建能打开任意程序的图标"
    HOMEPAGE_URL "https://github.com/ABitGinger/Entray"
    LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "Entray 是 Windows 托盘程序，只能在 Windows 上构建。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 单配置生成器（Ninja / MinGW Makefiles）默认不给 CMAKE_BUILD_TYPE，这里兜底成 Release，
# 免得有人不小心打出没优化的版本。
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ============================================================================
# 关键：静态链接 C/C++ 运行时库。
#
# 老版本“打包后拷到别人电脑上打不开”的根因就在这里。CMake 默认用 /MD，生成的
# exe 会依赖 VCRUNTIME140.dll、MSVCP140.dll、api-ms-win-crt-*.dll；目标机器只要
# 没装 VC++ 运行库，就会直接报“找不到 VCRUNTIME140.dll”而无法启动。历史上还额外
# 雪上加霜地发布了 Debug 版本（依赖 ucrtbased.dll，那个几乎只在开发机上存在）。
#
# 改成 /MT 之后运行时会被整体编进 exe，真正做到单文件、免安装。
# 注意 CMP0091（CMAKE_MSVC_RUNTIME_LIBRARY 生效）需要 CMake 3.15+。
# ============================================================================
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(Entray WIN32
    src/main.cpp
    src/app.cpp
    src/app.h
    src/config.cpp
    src/config.h
    src/process.cpp
    src/process.h
    src/util.cpp
    src/util.h
    src/common.h
    src/resource.h
    src/version.h
    resources/app.rc)

target_include_directories(Entray PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(Entray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    # 只用 Windows 7 开始就提供的 API，避免无意间抬高系统要求。
    WINVER=0x0601
    _WIN32_WINNT=0x0601)

# 说明：
#   shell32  Shell_NotifyIcon / ShellExecuteEx / SHGetFileInfo / ExtractIconEx
#   ole32    COM 初始化，用于解析 .lnk 快捷方式
#   uuid     CLSID_ShellLink / IID_IShellLinkW / IID_IPersistFile
#   advapi32 注册表读写
#   comdlg32 GetOpenFileName
#   shlwapi  路径辅助
target_link_libraries(Entray PRIVATE
    shell32
    ole32
    uuid
    advapi32
    comdlg32
    shlwapi
    user32
    gdi32)

set_target_properties(Entray PROPERTIES
    OUTPUT_NAME "Entray"
    WIN32_EXECUTABLE TRUE
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

source_group("Source" FILES
    src/main.cpp src/app.cpp src/app.h src/config.cpp src/config.h
    src/process.cpp src/process.h src/util.cpp src/util.h
    src/common.h src/resource.h src/version.h)
source_group("Resources" FILES resources/app.rc resources/app.manifest resources/icon.ico)

if(MSVC)
    # /utf-8：源码里有 UTF-8 中文字符串字面量，必须显式告诉编译器按 UTF-8 解析。
    target_compile_options(Entray PRIVATE /utf-8 /W4 /MP)
    # 清单已经由 resources/app.rc 提供，禁止链接器再生成一份，避免出现重复的 RT_MANIFEST。
    target_link_options(Entray PRIVATE /MANIFEST:NO)
elseif(MINGW)
    target_compile_options(Entray PRIVATE -Wall -Wextra -municode)
    # -municode : 入口点使用 wWinMain
    # -static   : 连 libgcc / libstdc++ 一起静态链接，MinGW 产物同样是单文件
    target_link_options(Entray PRIVATE -municode -static -static-libgcc -static-libstdc++)
    # windres 不认 .rc 里的 #pragma code_page，得从命令行告诉它资源脚本是 UTF-8。
    # 否则版本信息里的中文会按系统 ANSI 代码页解码成乱码。
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")
endif()

install(TARGETS Entray RUNTIME DESTINATION .)

# ---- 打包：生成 Entray-<版本>-win64.zip，里面只有一个 exe ----
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Entray")
set(CPACK_PACKAGE_VENDOR "ABitGinger")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Entray-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Entray - 托盘快捷启动器（单文件免安装）")
include(CPack)
