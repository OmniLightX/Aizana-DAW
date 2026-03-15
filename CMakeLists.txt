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
FetchContent_MakeAvailable(ebur128)

# CLAP C API v1.2.0 — Header-only C API for CLAP plugin hosting (MIT license)
# Used for PLUG-03: custom AudioPluginFormat subclass wrapping direct CLAP C API
# (preferred over alpha juce_clap_hosting community module — see RESEARCH.md Pitfall 1)
FetchContent_Declare(clap_api
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.0)
FetchContent_MakeAvailable(clap_api)

# ---------------------------------------------------------------------------
# Cloudes application target
# ---------------------------------------------------------------------------

juce_add_gui_app(Cloudes
    PRODUCT_NAME    "Cloudes"
    BUNDLE_ID       "com.cloudes.daw"
    VERSION         "0.1.0"
)

target_sources(Cloudes PRIVATE
    src/app/Main.cpp
    src/app/MainWindow.cpp
    src/app/MainComponent.cpp
    src/app/ApplicationState.cpp
    src/engine/EditManager.cpp
    src/engine/ClipEditor.cpp
    src/engine/MidiRecorder.cpp
    src/engine/MixerEngine.cpp
    src/engine/AutomationEngine.cpp
    src/audio/AudioEngine.cpp
    src/audio/AudioRecorder.cpp
    # Plugin hosting (PLUG-01 through PLUG-04)
    src/plugins/ClapPluginFormat.cpp
    src/plugins/PluginScanner.cpp
    src/plugins/PluginManager.cpp
    # DSP effects (FX-01 through FX-04)
    src/fx/EQProcessor.cpp
    src/fx/CompressorProcessor.cpp
    src/fx/ReverbProcessor.cpp
    src/fx/LimiterProcessor.cpp
    # Metering + Export (plan 01-07, EXP-01 through EXP-04)
    src/metering/MeterProcessor.cpp
    src/engine/ExportPipeline.cpp
    # UI Shell (plan 01-08)
    src/ui/arrangement/TransportBar.cpp
    src/ui/arrangement/ArrangementView.cpp
    src/ui/arrangement/TrackHeader.cpp
    src/ui/arrangement/ClipComponent.cpp
    src/ui/mixer/MixerView.cpp
    src/ui/mixer/ChannelStrip.cpp
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
    # Plugin browser popup (plan 01-15: PLUG-01/02/03/04 UI entry point)
    src/ui/pluginbrowser/PluginBrowserPopup.cpp
    # Export dialog (plan 01-16: EXP-01, EXP-02, EXP-04)
    src/ui/export/ExportDialog.cpp
    # Automation lane UI (plan 01-18: MIX-04)
    src/ui/automation/AutomationLane.cpp
    src/ui/arrangement/TrackRow.cpp
)

target_include_directories(Cloudes PRIVATE
    src
)

target_compile_definitions(Cloudes PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Cloudes,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Cloudes,JUCE_VERSION>"
)

target_link_libraries(Cloudes PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    tracktion::tracktion_engine
    ebur128
)

# CLAP API is header-only; add include path so src/plugins can use clap/clap.h
# libebur128 header — FetchContent puts it in build/_deps, add include path explicitly
target_include_directories(Cloudes PRIVATE
    ${clap_api_SOURCE_DIR}/include
    ${ebur128_SOURCE_DIR}/ebur128
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
