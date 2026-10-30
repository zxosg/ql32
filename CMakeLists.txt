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
  function(add_cli_conformance_test name)
    add_test(
      NAME ${name}
      COMMAND ${CMAKE_COMMAND}
        -DQL32_EXECUTABLE=$<TARGET_FILE:ql32>
        -DTEST_NAME=${name}
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/${name}.bas
        -DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/${name}.out
        -DEXPECTED_STDERR_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/${name}.err
        -DEXPECTED_EXIT_CODE=${ARGN}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake
    )
  endfunction()

  add_cli_conformance_test(cli_stdin 0)
  add_cli_conformance_test(cli_file 0)
  add_cli_conformance_test(cli_language_slice 0)
  add_cli_conformance_test(cli_expression_rules 0)
  add_cli_conformance_test(cli_print_separators 0)
  add_cli_conformance_test(cli_block_if 0)
  add_cli_conformance_test(cli_if_type_error 1)
  add_cli_conformance_test(cli_for_ranges 0)
  add_cli_conformance_test(cli_for_inline 0)
  add_cli_conformance_test(cli_for_zero_step 1)
  add_cli_conformance_test(cli_repeat_control 0)
  add_cli_conformance_test(cli_repeat_inline 0)
  add_cli_conformance_test(cli_for_exit 0)
  add_cli_conformance_test(cli_functions 0)
  add_cli_conformance_test(cli_procedure_locals 0)
  add_cli_conformance_test(cli_arrays 0)
  add_cli_conformance_test(cli_array_bounds 1)
  add_cli_conformance_test(cli_select 0)
  add_cli_conformance_test(cli_program_commands 0)
  add_cli_conformance_test(cli_mixed_type_error 1)
  add_cli_conformance_test(cli_div_overflow 1)
  add_cli_conformance_test(cli_invalid_power 1)
  add_cli_conformance_test(cli_syntax_error 1)
  add_cli_conformance_test(cli_runtime_error 1)
endif()

