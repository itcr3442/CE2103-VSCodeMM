add_executable(specs specs.cpp)
target_link_libraries(specs ce2103::mm)

add_executable(stl stl.cpp)
target_link_libraries(stl ce2103::mm)
