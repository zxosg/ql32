if(NOT DEFINED QL32_EXECUTABLE OR NOT DEFINED SOURCE_FILE OR
   NOT DEFINED INPUT_FILE OR NOT DEFINED EXPECTED_STDOUT_FILE)
  message(FATAL_ERROR "QL32_EXECUTABLE, SOURCE_FILE, INPUT_FILE, and EXPECTED_STDOUT_FILE are required")
endif()

file(READ "${EXPECTED_STDOUT_FILE}" expected_stdout)
execute_process(
  COMMAND "${QL32_EXECUTABLE}" --console "${SOURCE_FILE}"
  INPUT_FILE "${INPUT_FILE}"
  RESULT_VARIABLE actual_exit_code
  OUTPUT_VARIABLE actual_stdout
  ERROR_VARIABLE actual_stderr
)

if(NOT "${actual_exit_code}" STREQUAL "0")
  message(FATAL_ERROR "expected exit 0, got ${actual_exit_code}; stderr: ${actual_stderr}")
endif()
if(NOT "${actual_stdout}" STREQUAL "${expected_stdout}")
  message(FATAL_ERROR "stdout mismatch\nexpected: [${expected_stdout}]\nactual:   [${actual_stdout}]")
endif()
if(NOT "${actual_stderr}" STREQUAL "")
  message(FATAL_ERROR "unexpected stderr: ${actual_stderr}")
endif()
