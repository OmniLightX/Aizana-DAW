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
    PRODUCT_NAME "Aizana Engine Tests"
)

target_sources(engine_tests PRIVATE
    test_undo.cpp
    test_clip_editing.cpp
    test_comping.cpp
    test_vca.cpp
    test_cue_mixes.cpp
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
    test_ai_agent_bridge.cpp
    test_ai_live_protocol.cpp
    test_sidechain_routing.cpp
    test_automation_curve.cpp
    test_editor_view_state.cpp
    # Plugin hosting tests (PLUG-01 through PLUG-04) — require JUCE + Tracktion + CLAP API
    test_vst3_scan.cpp
    test_au_scan.cpp
    test_clap_host.cpp
    test_plugin_chain_order.cpp
    test_oop_audio_ring.cpp
    test_oop_plugin_host_bridge.cpp
    test_oop_host_marshal.cpp
    test_oop_param_midi.cpp
    test_quantize.cpp
    test_plugin_param_automation.cpp
    test_tempo_engine.cpp
    test_edit_groups.cpp
    test_pan_value.cpp
    test_removable_media_guard.cpp
    # DSP effects tests (FX-01 through FX-04) — require juce::dsp
    test_eq_dsp.cpp
    test_compressor_dsp.cpp
    test_reverb_dsp.cpp
    test_limiter_dsp.cpp
    test_deesser_dsp.cpp
    test_delay_dsp.cpp
    test_gate_dsp.cpp
    test_saturator_dsp.cpp
    test_instruments_dsp.cpp
    # Hoer-Pakete fuer den Live-Testlauf. Alle Cases tragen "[.artifacts]" und
    # laufen daher NICHT im normalen Suite-Lauf — Aufruf: engine_tests "[artifacts]"
    render_artifacts.cpp
    # Dito auf ECHTEM Material: engine_tests "[realmaterial]" mit AIZANA_MATERIAL_DIR
    render_artifacts_real.cpp
    test_bounce_in_place.cpp
    test_clip_merge.cpp
    test_clip_gain_envelope.cpp
    test_tempo_map.cpp
    test_warp_bend.cpp
    test_vocal_tuner.cpp
    # Metering tests (EXP-03) — require JUCE + libebur128
    test_lufs_meter.cpp
    # Export tests (EXP-01, EXP-04) — require JUCE + Tracktion Renderer
    test_export_wav.cpp
    # Rename-Migration (Cloudes → Aizana, 2026-07-05)
    test_legacy_migration.cpp
    # M13: Autosave darf die Wiedergabe nicht unterbrechen (playback-safe Snapshot)
    test_autosave.cpp
    # Source files
    ../src/app/UIStateManager.cpp
    ../src/app/LegacyMigration.cpp
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
    ../src/ai/AiAgentBridge.cpp
    ../src/ai/AiLiveProtocol.cpp
    ../src/ai/OnnxModelRunner.cpp
    ../src/engine/EditManager.cpp
    ../src/engine/RemovableMediaGuard.cpp
    ../src/engine/TempoEngine.cpp
    ../src/engine/ClipEditor.cpp
    ../src/engine/ClipGainEnvelope.cpp
    ../src/engine/CompingEngine.cpp
    ../src/engine/TempoDetector.cpp
    ../src/engine/MidiRecorder.cpp
    ../src/engine/MixerEngine.cpp
    ../src/engine/WarpEngine.cpp
    ../src/engine/PitchDetector.cpp
    ../src/engine/VocalTuner.cpp
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
    ../src/plugins/oop/SharedAudioRing.cpp
    ../src/plugins/oop/PluginHostBridge.cpp
    ../src/plugins/oop/OobAudioMarshal.cpp
    ../src/plugins/oop/OutOfProcessPluginInstance.cpp
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
    ../src/fx/DelayProcessor.cpp
    ../src/fx/GateProcessor.cpp
    ../src/fx/SaturatorProcessor.cpp
    # Built-in instruments (P0)
    ../src/instruments/DrumMachineProcessor.cpp
    ../src/instruments/SynthProcessor.cpp
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
    JUCE_APPLICATION_NAME_STRING="AizanaEngineTests"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    # Suppress unnecessary Tracktion Engine UI/device features in test context
    TRACKTION_ENABLE_AUTOMAP=0
    TRACKTION_ENABLE_REWIRE=0
    # Timestretch backend — must match the Aizana app target (Phase 3, Task 0)
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    # Offline-Render headless: AizanaUIBehaviour pumpt den Message-Loop nur mit
    # diesem Define — ohne es schlaeft der Message-Thread und te::callBlocking
    # in der Render-Initialisierung laeuft in den Timeout (Bounce/Freeze-Tests).
    JUCE_MODAL_LOOPS_PERMITTED=1
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

# Expose CLAP include path (consistent with Aizana app target)
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
    AIZANA_TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    AIZANA_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    AIZANA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(AIZANA_ENABLE_ONNX)
    target_link_libraries(engine_tests PRIVATE onnxruntime)
    target_compile_definitions(engine_tests PRIVATE AIZANA_HAVE_ONNXRUNTIME=1)
    set_target_properties(engine_tests PROPERTIES BUILD_RPATH "${AIZANA_ORT_LIBDIR}")
endif()

include(CTest)
include(Catch)
catch_discover_tests(engine_tests)

# ---------------------------------------------------------------------------
# oop_host_smoke — Phase 5 E5.3b end-to-end cross-process test (real spawn).
# Dual-role binary: re-launches itself as the plugin-host worker, round-trips audio
# through a stub plugin over the SharedAudioRing, and checks the result. This covers the
# spawn + IPC + shm path that can't run inside engine_tests (no worker entry there).
# ---------------------------------------------------------------------------
juce_add_console_app(oop_host_smoke PRODUCT_NAME "oop_host_smoke")
target_sources(oop_host_smoke PRIVATE
    oop_host_smoke.cpp
    ../src/plugins/oop/SharedAudioRing.cpp
    ../src/plugins/oop/PluginHostBridge.cpp
    ../src/plugins/oop/OobAudioMarshal.cpp
    ../src/plugins/oop/OutOfProcessPluginInstance.cpp
    ../src/app/LegacyMigration.cpp)
target_include_directories(oop_host_smoke PRIVATE ../src)
target_compile_definitions(oop_host_smoke PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="oop_host_smoke"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")
target_link_libraries(oop_host_smoke PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_extra)
add_test(NAME oop_host_smoke COMMAND oop_host_smoke)
