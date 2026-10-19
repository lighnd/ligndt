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
include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_USE_LIBCXX ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
)
FetchContent_MakeAvailable(benchmark)

if(TARGET benchmark)
    target_compile_options(benchmark PRIVATE -Wno-c2y-extensions)
endif()
if(TARGET benchmark_main)
    target_compile_options(benchmark_main PRIVATE -Wno-c2y-extensions)
endif()

find_package(highway CONFIG QUIET)
if(NOT highway_FOUND)
    find_package(hwy CONFIG QUIET)
    if(NOT hwy_FOUND)
        message(FATAL_ERROR "Could not find Google Highway. Please run: sudo apt install libhwy-dev")
    endif()
endif()

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

set(LIGNDT_WARNINGS
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

set(LIGNDT_SANITIZERS
        $<$<CONFIG:Debug>:-O0 -g3 -fno-elide-constructors -fsanitize=address,undefined,leak -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-common -DDEBUG>
        $<$<CONFIG:Release>:-O3 -march=native -ffast-math -flto=thin -fvectorize -fslp-vectorize -fomit-frame-pointer -mllvm -inline-threshold=1000 -ffunction-sections -fdata-sections -DNDEBUG>
        $<$<CONFIG:MinSizeRel>:-Os -flto=thin -ffunction-sections -fdata-sections -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O3 -g -march=native -ffast-math -flto=thin -fvectorize -fslp-vectorize -fno-omit-frame-pointer -DNDEBUG>
)

set(LIGNDT_LINK_OPTS
        -Wl,--as-needed
        $<$<CONFIG:Debug>:-fsanitize=address,undefined,leak -fno-sanitize-recover=all>
        $<$<CONFIG:Release>:-flto=thin -Wl,-O2 -Wl,--gc-sections>
        $<$<CONFIG:MinSizeRel>:-flto=thin -Wl,-O2 -Wl,--gc-sections>
        $<$<CONFIG:RelWithDebInfo>:-flto=thin>
)

add_library(ligndt_core INTERFACE)
target_include_directories(ligndt_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ligndt_core INTERFACE
        OpenMP::OpenMP_CXX
        Threads::Threads
)

add_executable(ligndt main.cpp)
target_link_libraries(ligndt PRIVATE
        ligndt_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Vulkan::Vulkan
        glm::glm
        Boost::filesystem
        hwy::hwy
)
target_compile_options(ligndt PRIVATE ${LIGNDT_WARNINGS} ${LIGNDT_SANITIZERS})
target_link_options(ligndt PRIVATE ${LIGNDT_LINK_OPTS})

enable_testing()

add_executable(ligndt_tests tests/entity_manager_tests.cc)
target_link_libraries(ligndt_tests PRIVATE ligndt_core)
target_compile_options(ligndt_tests PRIVATE
        ${LIGNDT_SANITIZERS}
        -Wall -Wextra -Wpedantic
        -Wno-error
)
add_test(NAME HardcoreEntityTests COMMAND ligndt_tests)

add_executable(ligndt_bench benchmarks/bench_entity_manager.cc)
target_link_libraries(ligndt_bench PRIVATE
        ligndt_core
        benchmark::benchmark
        benchmark::benchmark_main
)
target_compile_options(ligndt_bench PRIVATE
        -O3
        -march=native
        -DNDEBUG
        -Wall -Wextra
        -Wno-error
)
