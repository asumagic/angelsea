# SPDX-License-Identifier: BSD-2-Clause

add_subdirectory(vendor/Catch2/)

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