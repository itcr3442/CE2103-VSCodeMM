cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)
project("nlohmann's JSON for Modern C++")

add_library(json INTERFACE)
add_library(nlohmann::json ALIAS json)

target_include_directories(json INTERFACE include)
target_compile_features(json INTERFACE cxx_std_11)
