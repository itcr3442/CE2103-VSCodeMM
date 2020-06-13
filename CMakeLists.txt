cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)
project("CE2103 VSCode Memory Manager")

find_package(Threads REQUIRED)
find_package(Doxygen)

add_subdirectory(src)
add_subdirectory(deps)
add_subdirectory(examples)

if(DOXYGEN_FOUND)
	SET(DOXYGEN_PRIVATE YES)
	SET(DOXYGEN_EXTRACT_ALL YES)
	SET(DOXYGEN_FILE_PATTERNS "*.hpp,*.cpp")
	SET(DOXYGEN_EXTRACT_ANON_NSPACES YES)
	doxygen_add_docs(docs src include deps)
endif()
