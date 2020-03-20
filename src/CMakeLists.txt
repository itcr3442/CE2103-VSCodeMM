add_library(ce2103_vscodemm SHARED gc.cpp misc.cpp)
add_library(ce2103::mm ALIAS ce2103_vscodemm)

target_include_directories(ce2103_vscodemm PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(ce2103_vscodemm PUBLIC Threads::Threads ce2103::common)
set_target_properties(ce2103_vscodemm PROPERTIES POSITION_INDEPENDENT_CODE ON)
