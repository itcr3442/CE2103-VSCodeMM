cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)
project("CE2103 VSCode Memory Manager")

find_package(Threads REQUIRED)

add_subdirectory(src)
add_subdirectory(deps)
