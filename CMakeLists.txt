# Created by Gleb Obitotsky on 18.10.2026.
#
# Copyright 2026 Gleb Obitotsky,
#           2026 lighnd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://apache.org
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 4.3)
project(ligndt LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Please use clang")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-stdlib=libc++ -fcolor-diagnostics)
add_link_options(-stdlib=libc++ -fuse-ld=lld)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_PREFIX_PATH
        "/usr/lib/qt6"
        "/usr/lib/x86_64-linux-gnu/cmake/Qt6"
        "/usr/lib/x86_64-linux-gnu"
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS filesystem)

find_package(GLM NAMES glm GLM CONFIG QUIET)
if(NOT GLM_FOUND)
    find_path(GLM_INCLUDE_DIR NAMES glm/glm.hpp)
    if(NOT GLM_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find GLM. Please run: sudo apt install libglm-dev")
    endif()
    add_library(glm::glm INTERFACE IMPORTED)
    set_target_properties(glm::glm PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_DIR}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${GLM_INCLUDE_DIR}"
    )
endif()

add_executable(ligndt main.cpp
        core/entity.h
        core/entity_manager.h
)

target_link_libraries(ligndt PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Vulkan::Vulkan
        Threads::Threads
        glm::glm
        OpenMP::OpenMP_CXX
        Boost::filesystem
)

target_compile_options(ligndt PRIVATE
        -Wno-unknown-warning-option
        -Werror
        -Wall -Wextra
        -Wpedantic
        -Wabstract-vbase-init
        -Wanon-enum-enum-conversion
        -Warray-bounds-pointer-arithmetic
        -Wcast-align
        -Wcast-qual
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wextra-semi
        -Wformat=2
        -Wimplicit-fallthrough
        -Wmisleading-indentation
        -Wmissing-field-initializers
        -Wnon-virtual-dtor
        -Wnull-dereference
        -Wold-style-cast
        -Woverloaded-virtual
        -Wshadow
        -Wthread-safety
        -Wuninitialized
        -Wunused
        -Wno-gcc-install-dir-libstdcxx
        -Wno-unused-command-line-argument
)

target_compile_options(ligndt PRIVATE
        $<$<CONFIG:Debug>:-O0 -g3 -fno-elide-constructors -fsanitize=address,undefined,leak -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-common -DDEBUG>
        $<$<CONFIG:Release>:-O3 -march=native -ffast-math -flto=thin -fvectorize -fslp-vectorize -fomit-frame-pointer -mllvm -inline-threshold=1000 -ffunction-sections -fdata-sections -DNDEBUG>
        $<$<CONFIG:MinSizeRel>:-Os -flto=thin -ffunction-sections -fdata-sections -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O3 -g -march=native -ffast-math -flto=thin -fvectorize -fslp-vectorize -fno-omit-frame-pointer -DNDEBUG>
)

target_link_options(ligndt PRIVATE
        -Wl,--as-needed
        $<$<CONFIG:Debug>:-fsanitize=address,undefined,leak -fno-sanitize-recover=all>
        $<$<CONFIG:Release>:-flto=thin -Wl,-O2 -Wl,--gc-sections>
        $<$<CONFIG:MinSizeRel>:-flto=thin -Wl,-O2 -Wl,--gc-sections>
        $<$<CONFIG:RelWithDebInfo>:-flto=thin>
)