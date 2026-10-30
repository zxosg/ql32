cmake_minimum_required(VERSION 3.20)

project(ql32 VERSION 0.1.0 LANGUAGES CXX)

option(QL32_BUILD_TESTS "Build QL/32 host tests" ON)

add_library(ql32_core INTERFACE)
target_compile_features(ql32_core INTERFACE cxx_std_20)

if(QL32_BUILD_TESTS)
  enable_testing()
endif()

