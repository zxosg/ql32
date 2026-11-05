if(NOT DEFINED QL32_EXECUTABLE OR NOT DEFINED QL32_SOURCE_DIR)
  message(FATAL_ERROR "QL32_EXECUTABLE and QL32_SOURCE_DIR are required")
endif()

set(corpus
  "cli_numeric_portable|0"
  "cli_expression_rules|0"
  "cli_functions|0"
  "cli_procedure_locals|0"
  "cli_arrays|0"
  "cli_array_by_reference|0"
  "cli_select_edges|0"
  "cli_language_slice|0"
  "cli_call_depth|1"
  "cli_array_bounds|1"
  "cli_div_overflow|1"
)

foreach(entry IN LISTS corpus)
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 TEST_NAME)
  list(GET fields 1 EXPECTED_EXIT_CODE)
  set(SOURCE_FILE "${QL32_SOURCE_DIR}/tests/cli/${TEST_NAME}.bas")
  set(EXPECTED_STDOUT_FILE "${QL32_SOURCE_DIR}/tests/cli/${TEST_NAME}.out")
  set(EXPECTED_STDERR_FILE "${QL32_SOURCE_DIR}/tests/cli/${TEST_NAME}.err")
  include("${QL32_SOURCE_DIR}/tests/cli_test.cmake")
endforeach()

message(STATUS "Curated QL/32 golden corpus passed (${corpus_LENGTH} cases)")
