// test_export_wav.cpp — EXP-01: Stereo mixdown export; EXP-04: Format/sample rate selection
// Status: STUB — implementation added in plan 01-07 (export + metering)
//
// Tests Tracktion Engine Renderer for offline export.
// WAV output must be bit-exact with the offline render for a known test Edit.
// 96kHz/24-bit export must produce a correctly headered WAV file.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("ExportWAV: rendered output matches expected offline render checksum", "[export][EXP-01][stub]") {
    SKIP("Stub — implement when RenderPipeline is available (plan 01-07)");
}

TEST_CASE("ExportWAV: 96kHz/24-bit export produces valid WAV header with correct sample rate", "[export][EXP-04][stub]") {
    SKIP("Stub — implement when RenderPipeline is available (plan 01-07)");
}

TEST_CASE("ExportWAV: 44.1kHz/16-bit and 48kHz/24-bit exports both produce valid WAV files", "[export][EXP-04][stub]") {
    SKIP("Stub — implement when RenderPipeline is available (plan 01-07)");
}
