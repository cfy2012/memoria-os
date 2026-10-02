# Memoria OS 根 CMake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/components"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(memoria_os LANGUAGES CXX C)
