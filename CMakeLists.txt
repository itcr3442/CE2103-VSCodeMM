cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)
project("CE2103 VSCode Memory Manager")

find_package(Threads REQUIRED)
find_package(Doxygen REQUIRED)

add_subdirectory(src)
add_subdirectory(deps)
add_subdirectory(examples)

doxygen_add_docs(docs src include deps)
