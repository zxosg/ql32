if(NOT DEFINED QL32_EXECUTABLE OR NOT DEFINED SOURCE_FILE)
  message(FATAL_ERROR "QL32_EXECUTABLE and SOURCE_FILE are required")
endif()

file(READ "${EXPECTED_STDOUT_FILE}" expected_stdout)
file(READ "${EXPECTED_STDERR_FILE}" expected_stderr)

if(TEST_NAME STREQUAL "cli_file" OR TEST_NAME STREQUAL "cli_language_slice")
  execute_process(
    COMMAND "${QL32_EXECUTABLE}" "${SOURCE_FILE}"
    RESULT_VARIABLE actual_exit_code
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
  )
else()
  execute_process(
    COMMAND "${QL32_EXECUTABLE}"
    INPUT_FILE "${SOURCE_FILE}"
    RESULT_VARIABLE actual_exit_code
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
  )
endif()

if(NOT "${actual_exit_code}" STREQUAL "${EXPECTED_EXIT_CODE}")
  message(FATAL_ERROR "expected exit ${EXPECTED_EXIT_CODE}, got ${actual_exit_code}; stderr: ${actual_stderr}")
endif()
if(NOT "${actual_stdout}" STREQUAL "${expected_stdout}")
  message(FATAL_ERROR "stdout mismatch\nexpected: [${expected_stdout}]\nactual:   [${actual_stdout}]")
endif()
if(NOT "${actual_stderr}" STREQUAL "${expected_stderr}")
  message(FATAL_ERROR "stderr mismatch\nexpected: [${expected_stderr}]\nactual:   [${actual_stderr}]")
endif()
