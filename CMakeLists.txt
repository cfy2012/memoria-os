# Memoria OS 根 CMake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/components"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# PROJECT_VER 显式固定（2026-10-06 豆包-2）：根治 ccache 缓存 __TIME__ 导致的 app_desc 垃圾版本串，
# OTA 本地/远端版本比对不再拿 "Oct 4 2026" 这种串当版本号。发版时手动改这里。
set(PROJECT_VER "1.4.0")
project(memoria_os VERSION ${PROJECT_VER} LANGUAGES CXX C)
