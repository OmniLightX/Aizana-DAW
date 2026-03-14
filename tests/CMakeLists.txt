cmake_minimum_required(VERSION 3.25)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1)
FetchContent_MakeAvailable(Catch2)

# One test executable containing all Phase 1 stub test files.
# Each stub uses SKIP() so ctest reports SKIPPED, not FAILED.
# Tests are filled in by plans 01-02 through 01-07.
add_executable(all_tests
    test_undo.cpp
    test_clip_editing.cpp
    test_channel_strip.cpp
    test_automation_curve.cpp
    test_sidechain_routing.cpp
    test_eq_dsp.cpp
    test_compressor_dsp.cpp
    test_reverb_dsp.cpp
    test_limiter_dsp.cpp
    test_lufs_meter.cpp
    test_export_wav.cpp
    test_multitrack_render.cpp
    test_midi_recording.cpp
    test_vst3_scan.cpp
    test_au_scan.cpp
    test_clap_host.cpp
    test_plugin_chain_order.cpp
)

target_link_libraries(all_tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(all_tests)
