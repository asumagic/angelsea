# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.10)

project(angelsea)

# TODO: allow selecting these from system

#### GLOBAL COMPILER CONFIG

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
endif()

#### DEPENDENCIES

# Configure fmtlib build
add_subdirectory(vendor/fmt/)

# Configure MIR build
# Upstream CMakeLists doesn't allow us from excluding tests and such
set(MIR_ROOT "vendor/mir/")
add_library(
    mir
    OBJECT
    ${MIR_ROOT}/mir.c
    ${MIR_ROOT}/mir-gen.c
    ${MIR_ROOT}/c2mir/c2mir.c
    ${MIR_ROOT}/mir.h
    ${MIR_ROOT}/mir-gen.h
    ${MIR_ROOT}/c2mir/c2mir.h
)
target_include_directories(mir PUBLIC ${MIR_ROOT})

# Configure AngelScript build
set(ANGELSCRIPT_ROOT "vendor/angelscript/sdk")
set(ANGELSCRIPT_ADDON_ROOT "vendor/angelscript/sdk/add_on")
add_subdirectory(${ANGELSCRIPT_ROOT}/angelscript/projects/cmake)

# Configure AngelScript add-ons
add_library(angelscript-addons STATIC
    ${ANGELSCRIPT_ADDON_ROOT}/scriptarray/scriptarray.cpp
    ${ANGELSCRIPT_ADDON_ROOT}/scriptbuilder/scriptbuilder.cpp
    ${ANGELSCRIPT_ADDON_ROOT}/scriptstdstring/scriptstdstring.cpp
)
target_include_directories(angelscript-addons PUBLIC ${ANGELSCRIPT_ADDON_ROOT})
target_link_libraries(angelscript-addons PRIVATE angelscript)

#### Angelsea

add_library(
    angelsea
    STATIC
    src/angelsea/jit.cpp
    src/angelsea/detail/jitcompiler.cpp
    src/angelsea/detail/jitfunction.cpp
    src/angelsea/detail/bytecode2c.cpp
)
target_link_libraries(angelsea PRIVATE fmt mir angelscript)
target_include_directories(angelsea PUBLIC include/)
target_compile_features(angelsea PUBLIC cxx_std_20)

# AngelScript CMake module doesn't give us the include
target_include_directories(angelsea PUBLIC ${ANGELSCRIPT_ROOT}/angelscript/include)
target_include_directories(
    angelsea
    PRIVATE
    ${ANGELSCRIPT_ROOT}/angelscript/source
)

option(ENABLE_TESTING "Enable test building and support via Catch2" ON)

if (ENABLE_TESTING)
    add_subdirectory(tests)
endif()