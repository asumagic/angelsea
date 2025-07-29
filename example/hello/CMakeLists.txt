cmake_minimum_required(VERSION 3.10)

project(hello-angelsea)

# if you use Angelsea as a submodule, you would just do:
#     add_subdirectory(./path/to/angelsea)
add_subdirectory(../../ "./angelsea")

# add script add-ons we're using for this demo
set(ANGELSCRIPT_ADDON_ROOT "${ASEA_ANGELSCRIPT_ROOT}/add_on")
add_library(angelscript-addons STATIC
    ${ANGELSCRIPT_ADDON_ROOT}/scriptbuilder/scriptbuilder.cpp
    ${ANGELSCRIPT_ADDON_ROOT}/scriptstdstring/scriptstdstring.cpp
)
target_include_directories(angelscript-addons PUBLIC ${ANGELSCRIPT_ADDON_ROOT})
target_link_libraries(angelscript-addons PRIVATE asea_angelscript)

# configure our hello world
add_executable(hellojit main.cpp)
target_link_libraries(hellojit PRIVATE asea_angelscript angelsea angelscript-addons)