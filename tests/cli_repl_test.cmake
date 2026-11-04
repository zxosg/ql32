if(NOT DEFINED QL32_EXECUTABLE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED EXPECTED_STDOUT_FILE)
  message(FATAL_ERROR "QL32_EXECUTABLE, INPUT_FILE, and EXPECTED_STDOUT_FILE are required")
endif()
file(READ "${EXPECTED_STDOUT_FILE}" expected_stdout)
execute_process(
  COMMAND "${QL32_EXECUTABLE}" --console
  INPUT_FILE "${INPUT_FILE}"
  WORKING_DIRECTORY "${WORKING_DIRECTORY}"
  OUTPUT_VARIABLE actual_stdout
  ERROR_VARIABLE actual_stderr
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "REPL exited with ${result}; stderr: ${actual_stderr}")
endif()
if(NOT actual_stdout STREQUAL expected_stdout)
  message(FATAL_ERROR "REPL output mismatch.\nExpected:\n${expected_stdout}\nActual:\n${actual_stdout}\nStderr:\n${actual_stderr}")
endif()
if(NOT actual_stderr STREQUAL "")
  message(FATAL_ERROR "REPL emitted diagnostics: ${actual_stderr}")
endif()
