# Apply only to the verified recording-error patch baseline.
set(_te "${CMAKE_CURRENT_LIST_DIR}/../extern/tracktion_engine")
set(_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/tracktion-recording-monitoring.patch")
file(SHA256 "${_te}/modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.cpp" _hash)
if(_hash STREQUAL "2394236d43457019764a494dae30b83d16c2c9a2834e6642839812d2477d1ab9")
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${_patch}"
                    WORKING_DIRECTORY "${_te}" RESULT_VARIABLE _result ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Recording monitoring patch failed: ${_error}")
    endif()
elseif(NOT _hash STREQUAL "087975486d87358f154a77a2c2a9f383fa9ebed9d92201d73074d6bdb0577dd4")
    message(FATAL_ERROR "Unrecognised recording monitoring source; preserve local edits and review the patch")
endif()
