cmake_minimum_required(VERSION 3.10)

project(hello-angelsea)

# if you use Angelsea as a submodule, you would just do:
#     add_subdirectory(./path/to/angelsea)
add_subdirectory(../../ "./angelsea")

add_executable(hellojit main.cpp)
target_link_libraries(hellojit PRIVATE angelsea)