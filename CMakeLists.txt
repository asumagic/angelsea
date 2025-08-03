# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.10)

project(angelsea)

#### GLOBAL COMPILER CONFIG

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-Og -fsanitize=address)
        add_link_options(-Og -fsanitize=address)
    endif()
endif()

#### DEPENDENCIES

option(ASEA_FMT_SYSTEM "Disable building of vendored libfmt, and look it up via CMake instead" OFF)
option(ASEA_FMT_EXTERNAL "Disable CMake lookup of libfmt when -DASEA_FMT_SYSTEM=1 is set, and find headers via -DASEA_FMT_ROOT" OFF)

# Configure fmtlib build
set(
    ASEA_FMT_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/fmt"
    CACHE STRING
    "When building vendored libfmt, its root path"
)
if (ASEA_FMT_SYSTEM)
    if (NOT ASEA_FMT_EXTERNAL)
        find_package(fmt)
        set(ASEA_FMT_TARGET "fmt")
    else()
        add_library(asea_fmt INTERFACE)
        target_include_directories(asea_fmt INTERFACE ${ASEA_FMT_ROOT}/include)
        set(ASEA_FMT_TARGET "asea_fmt")
    endif()
else()
    message("angelsea: Using vendored libfmt at ${ASEA_FMT_ROOT}")
    add_subdirectory(${ASEA_FMT_ROOT})
    set(ASEA_FMT_TARGET "fmt")
endif()

# Configure MIR build
# Upstream CMakeLists doesn't allow us from excluding tests and such
option(ASEA_MIR_SYSTEM "Disable building of vendored MIR. Make sure to provide -DASEA_MIR_ROOT" OFF)

# Disable textual format scanning
add_compile_definitions(MIR_NO_SCAN=1)
add_compile_definitions(MIR_NO_BIN_COMPRESSION=1)
# add_compile_definitions(MIR_NO_IO=1)  # FIXME: broken build because of c2mir

set(
    ASEA_MIR_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mir/"
    CACHE STRING
    "Root path to MIR for includes (and sources when -DASEA_MIR_SYSTEM=0)"
)
if (ASEA_MIR_SYSTEM)
    add_library(asea_mir INTERFACE)
    target_include_directories(asea_mir INTERFACE ${ASEA_MIR_ROOT})
else()
    message("angelsea: Using vendored MIR at ${ASEA_MIR_ROOT}")
    add_library(asea_mir STATIC
        ${ASEA_MIR_ROOT}/mir.c
        ${ASEA_MIR_ROOT}/mir-gen.c
        ${ASEA_MIR_ROOT}/c2mir/c2mir.c
        ${ASEA_MIR_ROOT}/mir.h
        ${ASEA_MIR_ROOT}/mir-gen.h
        ${ASEA_MIR_ROOT}/c2mir/c2mir.h
    )
    target_include_directories(asea_mir PUBLIC ${ASEA_MIR_ROOT})
endif()

# Configure AngelScript build
# TODO: could probably use find_package too?
option(ASEA_ANGELSCRIPT_SYSTEM "Disable building of vendored AngelScript. Make sure to provide -ASEA_ANGELSCRIPT_ROOT" OFF)

set(
    ASEA_ANGELSCRIPT_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/angelscript/sdk"
    CACHE STRING
    "Root directory of the AngelScript SDK in use"
)

if (NOT ASEA_ANGELSCRIPT_SYSTEM)
    message("angelsea: Using vendored AngelScript at ${ASEA_ANGELSCRIPT_ROOT}")
    add_subdirectory(${ASEA_ANGELSCRIPT_ROOT}/angelscript/projects/cmake)
    if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(angelscript PRIVATE -fno-strict-aliasing)
    endif()
endif()

# AngelScript CMake module doesn't give us the include, make a target that does
add_library(asea_angelscript INTERFACE)
target_include_directories(asea_angelscript INTERFACE ${ASEA_ANGELSCRIPT_ROOT}/angelscript/include)

# Target which also provides the internal header include path
add_library(asea_angelscript_internal INTERFACE)
target_link_libraries(asea_angelscript_internal INTERFACE asea_angelscript)
target_include_directories(asea_angelscript_internal INTERFACE ${ASEA_ANGELSCRIPT_ROOT}/angelscript/source)

if (NOT ASEA_ANGELSCRIPT_SYSTEM)
    target_link_libraries(asea_angelscript INTERFACE angelscript)
endif()

#### Angelsea

add_library(angelsea STATIC
    src/angelsea/jit.cpp
    src/angelsea/detail/mirjit.cpp
    src/angelsea/detail/runtime.cpp
    src/angelsea/detail/bytecode2c.cpp
    src/angelsea/detail/bytecodedisasm.cpp
)
target_link_libraries(angelsea PRIVATE ${ASEA_FMT_TARGET} asea_mir asea_angelscript_internal)
target_include_directories(angelsea PUBLIC include/)
target_compile_features(angelsea PUBLIC cxx_std_20)

if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(angelsea PRIVATE -Wall -Wextra)
endif()

option(ASEA_ENABLE_TESTING "Enable test building and support via Catch2" OFF)

if (ASEA_ENABLE_TESTING)
    add_subdirectory(tests)
endif()