cmake_minimum_required(VERSION 3.25)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1)
FetchContent_MakeAvailable(Catch2)

# ---------------------------------------------------------------------------
# engine_tests — Tests requiring JUCE + Tracktion Engine linkage.
# Covers AUDIO-03, AUDIO-04, AUDIO-05, MIX-01, MIX-02, MIX-03, MIX-04,
# PLUG-01 through PLUG-04, FX-01 through FX-04, EXP-01 through EXP-04.
# Runs headless — no display or audio hardware required.
# ---------------------------------------------------------------------------
juce_add_console_app(engine_tests
    PRODUCT_NAME "Cloudes Engine Tests"
)

target_sources(engine_tests PRIVATE
    test_undo.cpp
    test_clip_editing.cpp
    test_comping.cpp
    test_vca.cpp
    test_event_fx.cpp
    test_tempo_detector.cpp
    test_auto_tempo.cpp
    test_markers.cpp
    test_freeze.cpp
    test_mix_plan.cpp
    test_mixing_engine.cpp
    test_mix_apply.cpp
    test_onnx_backend.cpp
    test_mix_data_logger.cpp
    test_plugin_generator.cpp
    test_faust_compiler.cpp
    test_faust_emitter.cpp
    test_stem_analyzer.cpp
    test_session_planner.cpp
    test_stem_import.cpp
    test_stem_separation.cpp
    test_reference_profile.cpp
    test_midi_recording.cpp
    test_multitrack_render.cpp
    test_channel_strip.cpp
    test_input_buses.cpp
    test_ai_command_bridge.cpp
    test_ui_state_manager.cpp
    test_intent_engine.cpp
    test_heuristic_feedback.cpp
    test_ai_engine.cpp
    test_sidechain_routing.cpp
    test_automation_curve.cpp
    # Plugin hosting tests (PLUG-01 through PLUG-04) — require JUCE + Tracktion + CLAP API
    test_vst3_scan.cpp
    test_au_scan.cpp
    test_clap_host.cpp
    test_plugin_chain_order.cpp
    # DSP effects tests (FX-01 through FX-04) — require juce::dsp
    test_eq_dsp.cpp
    test_compressor_dsp.cpp
    test_reverb_dsp.cpp
    test_limiter_dsp.cpp
    test_deesser_dsp.cpp
    # Metering tests (EXP-03) — require JUCE + libebur128
    test_lufs_meter.cpp
    # Export tests (EXP-01, EXP-04) — require JUCE + Tracktion Renderer
    test_export_wav.cpp
    # Source files
    ../src/app/UIStateManager.cpp
    ../src/ai/StemAnalyzer.cpp
    ../src/ai/SessionPlanner.cpp
    ../src/ai/MixingEngine.cpp
    ../src/ai/MixApplyEngine.cpp
    ../src/ai/MixDataLogger.cpp
    ../src/generator/PluginGenerator.cpp
    ../src/generator/FaustCompiler.cpp
    ../src/generator/PluginSpec.cpp
    ../src/generator/FaustEmitter.cpp
    ../src/generator/LocalComposer.cpp
    ../src/ai/StemImportEngine.cpp
    ../src/ai/StemSeparationEngine.cpp
    ../src/ai/ReferenceAnalyzer.cpp
    ../src/ai/IntentEngine.cpp
    ../src/ai/HeuristicFeedbackEngine.cpp
    ../src/ai/AIEngine.cpp
    ../src/ai/OnnxModelRunner.cpp
    ../src/engine/EditManager.cpp
    ../src/engine/ClipEditor.cpp
    ../src/engine/CompingEngine.cpp
    ../src/engine/TempoDetector.cpp
    ../src/engine/MidiRecorder.cpp
    ../src/engine/MixerEngine.cpp
    ../src/engine/InputBusManager.cpp
    ../src/engine/AICommandBridge.cpp
    ../src/engine/AutomationEngine.cpp
    ../src/audio/AudioRecorder.cpp
    ../src/audio/AudioEngine.cpp
    ../src/audio/MetronomeGenerator.cpp
    ../src/plugins/ClapPluginFormat.cpp
    ../src/plugins/BuiltInPluginFormat.cpp
    ../src/plugins/PluginScanner.cpp
    ../src/plugins/OutOfProcessPluginScanner.cpp
    ../src/plugins/PluginManager.cpp
    # UI components required by FX processors in test context
    ../src/ui/eq/EQComponent.cpp
    ../src/ui/eq/SpectrumAnalyzer.cpp
    ../src/metering/MasterMeterAudioProcessor.cpp
    # DSP effects implementations (FX-01 through FX-04)
    ../src/fx/EQProcessor.cpp
    ../src/fx/CompressorProcessor.cpp
    ../src/fx/ReverbProcessor.cpp
    ../src/fx/LimiterProcessor.cpp
    ../src/fx/DeEsserProcessor.cpp
    # Metering + Export implementations (plan 01-07)
    ../src/metering/MeterProcessor.cpp
    ../src/engine/ExportPipeline.cpp
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
    # Timestretch backend — must match the Cloudes app target (Phase 3, Task 0)
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
)

target_link_libraries(engine_tests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    tracktion::tracktion_engine
    ebur128
    demucs_inference
)

# Expose CLAP include path (consistent with Cloudes app target)
target_include_directories(engine_tests PRIVATE
    ${clap_api_SOURCE_DIR}/include
)

# libebur128 header — FetchContent puts it in build/_deps, expose same path as main target
target_include_directories(engine_tests PRIVATE
    ${ebur128_SOURCE_DIR}/ebur128
)

# ONNX Runtime — same imported target as the app (declared GLOBAL in the root).
# Resource dir exposes the dummy .onnx fixture used by test_onnx_backend.cpp.
target_compile_definitions(engine_tests PRIVATE
    CLOUDES_TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    CLOUDES_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    CLOUDES_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(CLOUDES_ENABLE_ONNX)
    target_link_libraries(engine_tests PRIVATE onnxruntime)
    target_compile_definitions(engine_tests PRIVATE CLOUDES_HAVE_ONNXRUNTIME=1)
    set_target_properties(engine_tests PROPERTIES BUILD_RPATH "${CLOUDES_ORT_LIBDIR}")
endif()

include(CTest)
include(Catch)
catch_discover_tests(engine_tests)
