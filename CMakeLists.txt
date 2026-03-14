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
    src/app/ApplicationState.cpp
    src/engine/EditManager.cpp
    src/audio/AudioEngine.cpp
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
target_include_directories(Cloudes PRIVATE
    ${clap_api_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
