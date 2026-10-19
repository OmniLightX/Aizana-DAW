# Observe the complete child process, including static destruction after main.
# Usage: cmake -DTEST_EXECUTABLE=/path/to/engine_tests '-DTEST_ARGS=[tag]' -P tests/RunEngineTests.cmake
if(NOT EXISTS "${TEST_EXECUTABLE}")
    message(FATAL_ERROR "Build engine_tests and provide TEST_EXECUTABLE first")
endif()
execute_process(COMMAND "${TEST_EXECUTABLE}" ${TEST_ARGS}
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _output)
message("${_output}")
if(_output MATCHES "JUCE Assertion failure|\\*\\*\\* Leaked objects detected:")
    message(FATAL_ERROR "JUCE diagnostic gate: assertion or leaked object reported")
endif()
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "engine_tests failed: ${_result}")
endif()
