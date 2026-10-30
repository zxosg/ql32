cmake_minimum_required(VERSION 3.20)

project(ql32 VERSION 0.1.0 LANGUAGES CXX)

option(QL32_BUILD_TESTS "Build QL/32 host tests" ON)

add_library(ql32_core
  src/language/interpreter.cpp
)
target_include_directories(ql32_core PUBLIC include)
target_compile_features(ql32_core PUBLIC cxx_std_20)

add_executable(ql32 src/main.cpp)
target_link_libraries(ql32 PRIVATE ql32_core)

if(QL32_BUILD_TESTS)
  enable_testing()
endif()

