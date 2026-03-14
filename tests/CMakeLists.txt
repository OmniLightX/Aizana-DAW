cmake_minimum_required(VERSION 3.25)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1)
FetchContent_MakeAvailable(Catch2)

# ---------------------------------------------------------------------------
# all_tests — Pure unit tests that do NOT require JUCE/Tracktion linkage.
# DSP, metering, plugin chain order, export, etc.
# These stub tests use SKIP() and compile with only Catch2.
# ---------------------------------------------------------------------------
add_executable(all_tests
    test_eq_dsp.cpp
    test_compressor_dsp.cpp
    test_reverb_dsp.cpp
    test_limiter_dsp.cpp
    test_lufs_meter.cpp
    test_export_wav.cpp
)

target_link_libraries(all_tests PRIVATE Catch2::Catch2WithMain)

# ---------------------------------------------------------------------------
# engine_tests — Tests requiring JUCE + Tracktion Engine linkage.
# Covers AUDIO-03, AUDIO-04, AUDIO-05, MIX-01, MIX-02, MIX-03, MIX-04.
# Runs headless — no display or audio hardware required.
# ---------------------------------------------------------------------------
juce_add_console_app(engine_tests
    PRODUCT_NAME "Cloudes Engine Tests"
)

target_sources(engine_tests PRIVATE
    test_undo.cpp
    test_clip_editing.cpp
    test_midi_recording.cpp
    test_multitrack_render.cpp
    test_channel_strip.cpp
    test_sidechain_routing.cpp
    test_automation_curve.cpp
    # Plugin hosting tests (PLUG-01 through PLUG-04) — require JUCE + Tracktion + CLAP API
    test_vst3_scan.cpp
    test_au_scan.cpp
    test_clap_host.cpp
    test_plugin_chain_order.cpp
    # Source files
    ../src/engine/EditManager.cpp
    ../src/engine/ClipEditor.cpp
    ../src/engine/MidiRecorder.cpp
    ../src/engine/MixerEngine.cpp
    ../src/engine/AutomationEngine.cpp
    ../src/audio/AudioRecorder.cpp
    ../src/audio/AudioEngine.cpp
    ../src/plugins/ClapPluginFormat.cpp
    ../src/plugins/PluginScanner.cpp
    ../src/plugins/PluginManager.cpp
)

target_include_directories(engine_tests PRIVATE
    ../src
)

target_compile_definitions(engine_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="CloudesEngineTests"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    # Suppress unnecessary Tracktion Engine UI/device features in test context
    TRACKTION_ENABLE_AUTOMAP=0
    TRACKTION_ENABLE_REWIRE=0
)

target_link_libraries(engine_tests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    tracktion::tracktion_engine
)

# Expose CLAP include path (consistent with Cloudes app target)
target_include_directories(engine_tests PRIVATE
    ${clap_api_SOURCE_DIR}/include
)

include(CTest)
include(Catch)
catch_discover_tests(all_tests)
catch_discover_tests(engine_tests)
