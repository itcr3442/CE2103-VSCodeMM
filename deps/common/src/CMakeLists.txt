add_library(ce2103_common STATIC network.cpp hash.cpp rtti.cpp)
add_library(ce2103::common ALIAS ce2103_common)

target_include_directories(ce2103_common PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_features(ce2103_common PUBLIC cxx_std_17)
target_compile_options(ce2103_common PUBLIC -Wall -Wextra -Werror)
set_target_properties(ce2103_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
