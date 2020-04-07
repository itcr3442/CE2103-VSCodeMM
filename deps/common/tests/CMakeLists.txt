add_library(ce2103_testing STATIC main.cpp)
add_library(ce2103::testing ALIAS ce2103_testing)

target_include_directories(ce2103_testing PUBLIC include)

add_executable(run_tests list_tests.cpp)
target_link_libraries(run_tests ce2103::common ce2103::testing)
set_target_properties(run_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

add_test(ce2103_common run_tests)
enable_testing()
