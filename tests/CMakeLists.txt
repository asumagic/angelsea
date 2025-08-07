# SPDX-License-Identifier: BSD-2-Clause

set(CATCH2_ROOT "vendor/Catch2")
add_subdirectory(${CATCH2_ROOT})

# Configure AngelScript add-ons
set(ANGELSCRIPT_ADDON_ROOT "${ASEA_ANGELSCRIPT_ROOT}/add_on")
add_library(angelscript-addons STATIC
    ${ANGELSCRIPT_ADDON_ROOT}/scriptarray/scriptarray.cpp
    ${ANGELSCRIPT_ADDON_ROOT}/scriptbuilder/scriptbuilder.cpp
    ${ANGELSCRIPT_ADDON_ROOT}/scriptstdstring/scriptstdstring.cpp
)
target_include_directories(angelscript-addons PUBLIC ${ANGELSCRIPT_ADDON_ROOT})
target_link_libraries(angelscript-addons PRIVATE asea_angelscript)

add_executable(angelsea-tests
	abi.cpp
	booleans.cpp
	branching.cpp
	classmanip.cpp
	common.cpp
	enums.cpp
	floatmath.cpp
	funcdefs.cpp
	functions.cpp
	globals.cpp
	integermath.cpp
	megatests.cpp
	recursion.cpp
	typedefs.cpp
)

target_link_libraries(angelsea-tests PRIVATE asea_angelscript_internal angelsea angelscript-addons Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${CATCH2_ROOT}/extras)
include(CTest)
include(Catch)
catch_discover_tests(
	angelsea-tests
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)