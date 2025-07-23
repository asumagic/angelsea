# SPDX-License-Identifier: BSD-2-Clause

set(CATCH2_ROOT "vendor/Catch2")
add_subdirectory(${CATCH2_ROOT})

add_executable(angelsea-tests
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

target_link_libraries(angelsea-tests PRIVATE angelsea angelscript-addons Catch2::Catch2WithMain)

# list(APPEND CMAKE_MODULE_PATH ${CATCH2_ROOT}/extras)
# include(CTest)
# include(Catch)
# catch_discover_tests(
# 	angelsea-tests
# 	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
# )