cmake_minimum_required(VERSION 3.25)
project(Cloudes VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS Universal Binary:
# Configure with: cmake -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
# This produces a fat binary that runs natively on both Apple Silicon and Intel Macs.

# ---------------------------------------------------------------------------
# External dependencies via submodules
# ---------------------------------------------------------------------------

# JUCE 8.0.12 — CMake-native since JUCE 6; provides audio I/O, DSP, MIDI, GUI
# Must be added BEFORE tracktion_engine modules, which use JUCE targets.
add_subdirectory(extern/JUCE)

# Tracktion Engine v3.2.0 — Add only the modules/ directory, NOT the root CMakeLists.txt.
# The root CMakeLists.txt adds modules/juce (a submodule), which conflicts with
# our JUCE submodule. By adding modules/ directly, we skip the duplicate JUCE
# inclusion. JUCE is already available via the add_subdirectory(extern/JUCE) above.
# GPL3/Commercial dual-license; Cloudes is GPL3 (see LICENSE.md).
add_subdirectory(extern/tracktion_engine/modules)

# ---------------------------------------------------------------------------
# External dependencies via FetchContent
# ---------------------------------------------------------------------------

include(FetchContent)

# libebur128 v1.2.6 — EBU R128 LUFS/loudness measurement (MIT license)
# OVERRIDE_FIND_PACKAGE not needed; CMAKE_POLICY_VERSION_MINIMUM required because
# libebur128's CMakeLists.txt uses cmake_minimum_required(2.8) which CMake 3.30+ rejects.
set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
FetchContent_Declare(ebur128
    GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
    GIT_TAG        v1.2.6)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # force static — prevents missing-dylib crash at launch
FetchContent_MakeAvailable(ebur128)

# CLAP C API v1.2.0 — Header-only C API for CLAP plugin hosting (MIT license)
# Used for PLUG-03: custom AudioPluginFormat subclass wrapping direct CLAP C API
# (preferred over alpha juce_clap_hosting community module — see RESEARCH.md Pitfall 1)
FetchContent_Declare(clap_api
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.0)
FetchContent_MakeAvailable(clap_api)

# Eigen — header-only linear algebra, dependency of the vendored demucs.cpp
# inference library (extern/demucs_cpp). Pinned to the exact commit upstream
# demucs.cpp vendors so numerical behaviour matches the spike benchmark.
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        dd8c71e62852b2fe429edb6682ac91fd1c578a26)
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Populate(eigen)   # headers only — do NOT add Eigen's CMake project

# ---------------------------------------------------------------------------
# demucs.cpp inference library (vendored, MIT — see extern/demucs_cpp/README.md)
# Phase 3: stem separation (htdemucs 6-source / fine-tuned 4-source, ggml weights).
# CPU-only by design; runs on a background thread via StemSeparationEngine.
# ---------------------------------------------------------------------------
file(GLOB DEMUCS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extern/demucs_cpp/src/*.cpp)
add_library(demucs_inference STATIC ${DEMUCS_SOURCES})
target_include_directories(demucs_inference PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/demucs_cpp/src
    ${eigen_SOURCE_DIR})
target_compile_features(demucs_inference PUBLIC cxx_std_17)
# Eigen needs NDEBUG + optimisation in all configs we ship; -O3 keeps inference
# at the benchmarked ~1.6x realtime instead of >10x in unoptimised builds.
target_compile_options(demucs_inference PRIVATE -O3 -DNDEBUG)

# ---------------------------------------------------------------------------
# ONNX Runtime 1.20.1 — prebuilt macOS universal2 (arm64 + x86_64) with the
# CoreML execution provider (Apple Neural Engine). Phase 4 / Slice 5a: the real
# inference backend behind IInferenceBackend (OnnxModelRunner). We use Microsoft's
# prebuilt binary — CoreML EP is compiled into the macOS build; we do NOT build
# ORT from source. Toggle off with -DCLOUDES_ENABLE_ONNX=OFF to skip the download
# (OnnxModelRunner then compiles as a defined no-op stub).
# ---------------------------------------------------------------------------
option(CLOUDES_ENABLE_ONNX "Build with the ONNX Runtime + CoreML inference backend" ON)
if(CLOUDES_ENABLE_ONNX)
    FetchContent_Declare(onnxruntime_prebuilt
        URL      https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-osx-universal2-1.20.1.tgz
        URL_HASH SHA256=da4349e01a7e997f5034563183c7183d069caadc1d95f499b560961787813efd)
    FetchContent_MakeAvailable(onnxruntime_prebuilt)

    set(ORT_DIR   ${onnxruntime_prebuilt_SOURCE_DIR})
    set(ORT_DYLIB ${ORT_DIR}/lib/libonnxruntime.1.20.1.dylib)

    # IMPORTED GLOBAL so the tests/ subdirectory can link the same target.
    add_library(onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION             ${ORT_DYLIB}
        INTERFACE_INCLUDE_DIRECTORIES ${ORT_DIR}/include)

    set(CLOUDES_ORT_DYLIB  ${ORT_DYLIB}    CACHE INTERNAL "ONNX Runtime dylib path")
    set(CLOUDES_ORT_LIBDIR ${ORT_DIR}/lib  CACHE INTERNAL "ONNX Runtime lib dir (for RPATH)")
endif()

# ---------------------------------------------------------------------------
# Plugin Generator (Phase 4 / Iteration B, B1) — Text → Faust → CLAP.
# Compiles curated Faust DSP templates (generator/templates/*.dsp) to self-contained
# universal .clap at build time (recipe proven in Spike S2) and bundles them into the
# app. Gated: needs the Faust compiler (brew install faust). Off / Faust missing →
# generator feature compiles out (CLOUDES_HAVE_GENERATOR undefined), build stays green.
# The generator's clap/clap-helpers are fetched at main, ISOLATED from the hosting
# clap_api (1.2.0) — they meet only at the stable CLAP C-ABI when the .clap is loaded.
# ---------------------------------------------------------------------------
option(CLOUDES_BUILD_GENERATOR "Build the Faust→CLAP plugin generator templates" ON)
find_program(FAUST_EXECUTABLE faust)
set(CLOUDES_HAVE_GENERATOR 0)

if(CLOUDES_BUILD_GENERATOR AND FAUST_EXECUTABLE)
    execute_process(COMMAND ${FAUST_EXECUTABLE} --archdir
        OUTPUT_VARIABLE FAUST_ARCHDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(_faust_bin    ${FAUST_EXECUTABLE} DIRECTORY)
    get_filename_component(_faust_prefix ${_faust_bin}       DIRECTORY)
    set(FAUST_INCLUDE ${_faust_prefix}/include)   # Faust C++ runtime headers (NOT archdir)

    # Header-only; Populate (not MakeAvailable) so we don't add_subdirectory a SECOND
    # clap CMake project (target-name clash with clap_api above).
    FetchContent_Declare(clap_gen
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        main)
    FetchContent_Declare(clap_helpers
        GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
        GIT_TAG        main)
    FetchContent_Populate(clap_gen)
    FetchContent_Populate(clap_helpers)

    set(GEN_OUT ${CMAKE_BINARY_DIR}/generator)
    file(GLOB GEN_TEMPLATES ${CMAKE_CURRENT_SOURCE_DIR}/generator/templates/*.dsp)
    set(GEN_CLAPS "")
    foreach(tpl ${GEN_TEMPLATES})
        get_filename_component(_name ${tpl} NAME_WE)
        set(_out ${GEN_OUT}/${_name}.clap)
        add_custom_command(
            OUTPUT  ${_out}
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/generator/build_clap.sh
                    ${FAUST_EXECUTABLE} ${tpl} ${_out}
                    ${FAUST_ARCHDIR} ${FAUST_INCLUDE}
                    ${clap_gen_SOURCE_DIR}/include ${clap_helpers_SOURCE_DIR}/include
                    ${CMAKE_CURRENT_SOURCE_DIR}/generator/faust_gui_globals.cpp
                    ${GEN_OUT}/work/${_name}
            DEPENDS ${tpl} ${CMAKE_CURRENT_SOURCE_DIR}/generator/build_clap.sh
                    ${CMAKE_CURRENT_SOURCE_DIR}/generator/faust_gui_globals.cpp
            COMMENT "Faust→CLAP: ${_name}.clap"
            VERBATIM)
        list(APPEND GEN_CLAPS ${_out})
    endforeach()

    add_custom_target(cloudes_generator_templates ALL DEPENDS ${GEN_CLAPS})
    set(CLOUDES_HAVE_GENERATOR 1)
    message(STATUS "Cloudes: plugin generator ENABLED (Faust ${FAUST_EXECUTABLE})")
else()
    message(STATUS "Cloudes: plugin generator disabled (Faust not found or CLOUDES_BUILD_GENERATOR=OFF)")
endif()

# ---------------------------------------------------------------------------
# Cloudes application target
# ---------------------------------------------------------------------------

juce_add_gui_app(Cloudes
    PRODUCT_NAME                    "Cloudes"
    BUNDLE_ID                       "com.cloudes.daw"
    VERSION                         "0.1.0"
    MICROPHONE_PERMISSION_ENABLED   TRUE
    MICROPHONE_PERMISSION_TEXT      "Cloudes needs microphone access to record audio tracks."
)

target_sources(Cloudes PRIVATE
    src/app/Main.cpp
    src/app/MicrophonePermission.mm
    src/app/MainWindow.cpp
    src/app/MainComponent.cpp
    src/app/ApplicationState.cpp
    src/app/UIStateManager.cpp
    src/ai/StemAnalyzer.cpp
    src/ai/SessionPlanner.cpp
    src/ai/StemImportEngine.cpp
    src/ai/StemSeparationEngine.cpp
    src/ai/ReferenceAnalyzer.cpp
    src/ai/IntentEngine.cpp
    src/ai/HeuristicFeedbackEngine.cpp
    src/ai/AIEngine.cpp
    src/ai/OnnxModelRunner.cpp
    src/ai/MixingEngine.cpp
    src/ai/MixApplyEngine.cpp
    src/ai/MixDataLogger.cpp
    src/ai/AiAgentBridge.cpp
    src/ai/AiLiveProtocol.cpp
    src/ai/AiStudioLiveServer.cpp
    src/ui/aistudio/AiStudioPanel.cpp
    src/generator/PluginGenerator.cpp
    src/generator/FaustCompiler.cpp
    src/generator/PluginSpec.cpp
    src/generator/FaustEmitter.cpp
    src/generator/LocalComposer.cpp
    src/engine/EditManager.cpp
    src/engine/ClipEditor.cpp
    src/engine/CompingEngine.cpp
    src/engine/TempoDetector.cpp
    src/engine/MidiRecorder.cpp
    src/engine/MixerEngine.cpp
    src/engine/InputBusManager.cpp
    src/engine/AICommandBridge.cpp
    src/engine/AutomationEngine.cpp
    src/audio/AudioEngine.cpp
    src/audio/AudioRecorder.cpp
    src/audio/MetronomeGenerator.cpp
    # Plugin hosting (PLUG-01 through PLUG-04)
    src/plugins/ClapPluginFormat.cpp
    src/plugins/BuiltInPluginFormat.cpp
    src/plugins/PluginScanner.cpp
    src/plugins/OutOfProcessPluginScanner.cpp
    src/plugins/PluginManager.cpp
    # Out-of-process plugin hosting transport (E5 — plugin-hang hardening)
    src/plugins/oop/SharedAudioRing.cpp
    src/plugins/oop/PluginHostBridge.cpp
    src/plugins/oop/OobAudioMarshal.cpp
    src/plugins/oop/OutOfProcessPluginInstance.cpp
    # DSP effects (FX-01 through FX-04)
    src/fx/EQProcessor.cpp
    src/fx/CompressorProcessor.cpp
    src/fx/ReverbProcessor.cpp
    src/fx/LimiterProcessor.cpp
    src/fx/DeEsserProcessor.cpp
    # Metering + Export (plan 01-07, EXP-01 through EXP-04; plan 01-39 TE global tap)
    src/metering/MeterProcessor.cpp
    src/metering/MasterMeterAudioProcessor.cpp
    src/engine/ExportPipeline.cpp
    # UI Shell (plan 01-08)
    src/ui/arrangement/TransportBar.cpp
    src/ui/arrangement/ArrangementView.cpp
    src/ui/arrangement/MarkerLane.cpp
    src/ui/arrangement/TrackHeader.cpp
    src/ui/arrangement/ClipComponent.cpp
    src/ui/mixer/MixerView.cpp
    src/ui/mixer/ChannelStrip.cpp
    src/ui/mixer/MasterStrip.cpp
    src/ui/mixer/BusFXStrip.cpp
    src/ui/mixer/VCAStrip.cpp
    src/ui/mixer/SendSlotPanel.cpp
    # Comping editor (Phase 2+, Studio One take comping)
    src/ui/comping/CompingView.cpp
    # Inspector dock (Phase 2+, Studio One inspector)
    src/ui/inspector/InspectorPanel.cpp
    # Piano Roll editor (plan 01-09)
    src/ui/pianoroll/PianoRollView.cpp
    src/ui/pianoroll/NoteComponent.cpp
    src/ui/pianoroll/VelocityLane.cpp
    src/ui/pianoroll/CCLane.cpp
    # Plugin window management (plan 01-10: PLUG-01/02/03/04)
    src/ui/pluginwin/PluginWindowManager.cpp
    src/ui/pluginwin/FloatingPluginWindow.cpp
    src/ui/pluginwin/DockedPluginPanel.cpp
    # EQ UI + Spectrum analyzer (plan 01-10: FX-01)
    src/ui/eq/EQComponent.cpp
    src/ui/eq/SpectrumAnalyzer.cpp
    # Preferences dialog (plan 01-11: MIDI clock UI)
    src/ui/preferences/PreferencesDialog.cpp
    # Audio Preferences dialog (AudioDeviceSelectorComponent + latency display)
    src/ui/prefs/AudioPreferencesDialog.cpp
    # Plugin browser popup (plan 01-15: PLUG-01/02/03/04 UI entry point)
    src/ui/pluginbrowser/PluginBrowserPopup.cpp
    src/ui/pluginbrowser/GeneratedPluginsPanel.cpp
    # Export dialog (plan 01-16: EXP-01, EXP-02, EXP-04)
    src/ui/export/ExportDialog.cpp
    # Audio I/O Setup dialog (GAP-31: recording input assignment)
    src/ui/audiosetup/AudioIOSetupDialog.cpp
    # Automation lane UI (plan 01-18: MIX-04)
    src/ui/automation/AutomationLane.cpp
    src/ui/arrangement/TrackRow.cpp
    src/ui/beginner/BeginnerView.cpp
    src/ui/onboarding/OnboardingWizard.cpp
)

target_include_directories(Cloudes PRIVATE
    src
)

target_compile_definitions(Cloudes PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Timestretch backend (Phase 3, Task 0): SoundTouch is bundled inside the TE module
    # (modules/tracktion_engine/3rd_party/soundtouch) and LGPL — compatible with GPL3.
    # Without a backend, setAutoTempo/setTimeStretchMode/WarpTimeManager are silent no-ops.
    # Elastique stays off (commercial licence). Must match the engine_tests definition.
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    # Enable VST3 + AU hosting in Cloudes' OWN juce_audio_processors compilation so the
    # plugin-browser scanner (PluginScanner) can actually register and find them. Without
    # these, addDefaultFormats() adds nothing for the format and the user's library never
    # appears — independent of the in-process crash issue (Fix 4).
    #
    # AU (enabled 2026-06-07): the user's 1141-plugin library is largely AudioUnits, so
    # they stay invisible without this. Safe to enable now because (a) AUs are probed in
    # the crash/hang-isolated worker (60s watchdog → a stalling AU blacklists itself and
    # the scan continues), (b) incremental persistence means a slow first AU scan is
    # cached/resumable, and (c) the in-process fallback is now watchdog-bounded too. The
    # first AU scan is still slow (Apple validates each component) but completes + caches.
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Cloudes,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Cloudes,JUCE_VERSION>"
)

target_link_libraries(Cloudes PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    tracktion::tracktion_engine
    ebur128
    demucs_inference
    "$<$<PLATFORM_ID:Darwin>:-framework AVFoundation>"
)

# CLAP API is header-only; add include path so src/plugins can use clap/clap.h
# libebur128 header — FetchContent puts it in build/_deps, add include path explicitly
target_include_directories(Cloudes PRIVATE
    ${clap_api_SOURCE_DIR}/include
    ${ebur128_SOURCE_DIR}/ebur128
)

# ONNX Runtime: link + bundle the dylib into the .app (install name is @rpath/...).
if(CLOUDES_ENABLE_ONNX)
    target_link_libraries(Cloudes PRIVATE onnxruntime)
    target_compile_definitions(Cloudes PRIVATE CLOUDES_HAVE_ONNXRUNTIME=1)
    set_target_properties(Cloudes PROPERTIES
        BUILD_RPATH "${CLOUDES_ORT_LIBDIR};@executable_path/../Frameworks")
    add_custom_command(TARGET Cloudes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CLOUDES_ORT_DYLIB}"
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Frameworks/"
        VERBATIM)
endif()

# Plugin generator: build the template .clap before the app, then bundle them into
# Contents/Resources/Generator so the in-app generator can load them (Iteration B).
if(CLOUDES_HAVE_GENERATOR)
    add_dependencies(Cloudes cloudes_generator_templates)
    target_compile_definitions(Cloudes PRIVATE CLOUDES_HAVE_GENERATOR=1)
    add_custom_command(TARGET Cloudes POST_BUILD
        # Clear first so removed/renamed templates don't linger in the bundle.
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/Generator"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/Generator"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GEN_CLAPS}
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/Generator/"
        VERBATIM)

    # Runtime-generator toolchain (G1): bundle the CLAP/clap-helpers headers + GUI globals
    # so FaustCompiler can compile user-/AI-authored DSP at runtime (system `faust` provides
    # the rest). clang comes from the Command Line Tools.
    add_custom_command(TARGET Cloudes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/GeneratorToolchain"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${clap_gen_SOURCE_DIR}/include"
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/GeneratorToolchain/clap/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${clap_helpers_SOURCE_DIR}/include"
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/GeneratorToolchain/clap-helpers/include"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/generator/faust_gui_globals.cpp"
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/GeneratorToolchain/"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# cloudes-mcp — standalone MCP (stdio) server (Generator G4). Lets Claude Code /
# Codex author brand-new plugins for Cloudes from free text, reusing the SAME C++
# generator code (single source of truth). Console app, juce_core only. Built only
# when the generator toolchain is available (Faust present).
# ---------------------------------------------------------------------------
if(CLOUDES_HAVE_GENERATOR)
    juce_add_console_app(cloudes-mcp PRODUCT_NAME "cloudes-mcp")
    target_sources(cloudes-mcp PRIVATE
        src/mcp/CloudesMcpServer.cpp
        src/ai/AiLiveProtocol.cpp
        src/generator/FaustCompiler.cpp
        src/generator/FaustEmitter.cpp
        src/generator/LocalComposer.cpp
        src/generator/PluginSpec.cpp)
    target_include_directories(cloudes-mcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(cloudes-mcp PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(cloudes-mcp PRIVATE juce::juce_core)

    # Ship a GeneratorToolchain dir next to the binary so FaustToolchain::resolve() finds the
    # CLAP headers + GUI globals when the server runs outside the app bundle.
    add_custom_command(TARGET cloudes-mcp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:cloudes-mcp>/GeneratorToolchain"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${clap_gen_SOURCE_DIR}/include"
            "$<TARGET_FILE_DIR:cloudes-mcp>/GeneratorToolchain/clap/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${clap_helpers_SOURCE_DIR}/include"
            "$<TARGET_FILE_DIR:cloudes-mcp>/GeneratorToolchain/clap-helpers/include"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/generator/faust_gui_globals.cpp"
            "$<TARGET_FILE_DIR:cloudes-mcp>/GeneratorToolchain/"
        VERBATIM)

    # Bundle cloudes-mcp into the app so the in-app KI-Studio can spawn it (the app's own
    # Resources/GeneratorToolchain — bundled above — serves as its toolchain). cloudes-mcp
    # builds first (dependency); the copy runs in the app's POST_BUILD, after the bundle exists.
    add_dependencies(Cloudes cloudes-mcp)
    add_custom_command(TARGET Cloudes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:cloudes-mcp>"
            "$<TARGET_BUNDLE_CONTENT_DIR:Cloudes>/Resources/cloudes-mcp"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
