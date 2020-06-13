add_executable(run_mm_tests vsptr_tests.cpp)
target_link_libraries(run_mm_tests ce2103::mm ce2103::testing)
set_target_properties(run_mm_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

add_test(ce2103_vscodemm run_tests)
enable_testing()
