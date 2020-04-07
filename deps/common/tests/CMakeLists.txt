add_library(ce2103_testing STATIC main.cpp)
add_library(ce2103::testing ALIAS ce2103_testing)

target_include_directories(ce2103_testing PUBLIC include)
