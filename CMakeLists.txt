cmake_minimum_required(VERSION 4.3)
project(ligndt LANGUAGES C CXX)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++ -fuse-ld=lld)
    add_compile_options(-fcolor-diagnostics)
else()
    message(FATAL_ERROR "Please use clang")
endif()

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

add_executable(ligndt main.cpp)

target_link_libraries(ligndt PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Vulkan::Vulkan
)

target_compile_options(ligndt PRIVATE
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
        $<$<CONFIG:Debug>:-O0 -g3 -fno-elide-constructors -fsanitize=address,undefined,leak -fno-omit-frame-pointer -fno-common -DDEBUG>
        $<$<CONFIG:Release>:-O3 -march=native -ffast-math -flto=thin -fvectorize -fslp-vectorize -fomit-frame-pointer -mllvm -inline-threshold=1000 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -DNDEBUG>
        $<$<CONFIG:MinSizeRel>:-Os -flto=thin -fno-exceptions -fno-rtti -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O3 -g -march=native -ffast-math -flto=thin -fvectorize -fslp-vectorize -fno-omit-frame-pointer -DNDEBUG>
)

target_link_options(ligndt PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>
        $<$<CONFIG:Release>:-flto=thin -Wl,--gc-sections>
        $<$<CONFIG:MinSizeRel>:-flto=thin -Wl,--gc-sections>
        $<$<CONFIG:RelWithDebInfo>:-flto=thin>
)