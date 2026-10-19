# Third-Party Notices

This file summarizes third-party components used by Aizana and the release
obligations that matter for public source or binary distribution.

This is a technical release checklist, not legal advice. When shipping a public
binary, include this notice, the project license, the exact source for the
released commit, and the source or license text for each bundled dependency.

## Project License

Aizana source code is licensed under GPL-3.0-or-later. Aizana combines with
third-party libraries under their own licenses. Public releases must satisfy the
licenses of the combined work, including JUCE and Tracktion Engine.

## Runtime and Framework Dependencies

### JUCE 8

- Location: `extern/JUCE`
- License: AGPLv3 or commercial JUCE license
- Release note: Aizana can be public/open-source under the AGPL-compatible
  route. Do not describe JUCE 8 as GPL3-only. A proprietary Aizana build would
  require an appropriate commercial JUCE license.
- Upstream: https://github.com/juce-framework/JUCE
- License page: https://github.com/juce-framework/JUCE/blob/master/LICENSE.md

JUCE also contains third-party dependencies such as AudioUnitSDK, FLAC, Ogg
Vorbis, LV2, VST3 SDK, zlib, HarfBuzz, SheenBidi, and others. The authoritative
list is in `extern/JUCE/LICENSE.md`.

### Tracktion Engine

- Location: `extern/tracktion_engine`
- License: GPL3-or-later or commercial Tracktion Engine license
- Release note: GPL Aizana releases can use the GPL route. A proprietary or
  closed-source Aizana build would require a commercial Tracktion Engine
  license.
- Upstream: https://github.com/Tracktion/tracktion_engine
- License page: https://github.com/Tracktion/tracktion_engine/blob/develop/LICENSE.md

Aizana modifications by Aizanagi and the Aizana contributors, 20 October 2026:
`patches/tracktion-state-lifecycle.patch` preserves plugin bus layouts when
derived metadata is absent, validates layouts before changing processor buses,
initializes scene infrastructure before track Undo transactions, and avoids
plugin-state writes after Edit deletion. The patch is applied reproducibly by
`cmake/ApplyTracktionStatePatch.cmake`; these modifications use the same
GPL-3.0-or-later terms as the upstream files. Upstream copyright and license
notices remain intact.

### SoundTouch

- Location: bundled inside Tracktion Engine at
  `extern/tracktion_engine/modules/tracktion_engine/3rd_party/soundtouch`
- License: LGPL-2.1-or-later
- Use in Aizana: enabled through `TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1`
  for clip time-stretching.
- Release note: LGPL is compatible with GPL/AGPL distribution, but binary
  releases should include the SoundTouch notice and corresponding source offer.

### ONNX Runtime

- Location: downloaded by CMake when `AIZANA_ENABLE_ONNX=ON`
- License: MIT
- Use in Aizana: local inference backend, with CoreML execution provider on
  macOS when available.
- Upstream: https://github.com/microsoft/onnxruntime
- License page: https://github.com/microsoft/onnxruntime/blob/main/LICENSE
- Note: the prebuilt ONNX Runtime binary statically bundles its own third-party
  components; ship its `ThirdPartyNotices.txt` (included in the downloaded archive)
  alongside any binary release.

### Eigen

- Location: downloaded by CMake through `FetchContent`
- License: primarily MPL-2.0, with some compatible third-party files
- Use in Aizana: header-only dependency for the vendored `demucs.cpp`
  inference library.
- Upstream: https://gitlab.com/libeigen/eigen
- License notes: see Eigen's `COPYING.README`, `COPYING.MPL2`, and related
  copying files in the fetched source.

### libebur128

- Location: downloaded by CMake through `FetchContent`
- License: MIT
- Use in Aizana: EBU R128/LUFS loudness metering and reference analysis.
- Upstream: https://github.com/jiixyj/libebur128

### CLAP C API

- Location: downloaded by CMake through `FetchContent`
- License: MIT
- Use in Aizana: CLAP plugin hosting API.
- Upstream: https://github.com/free-audio/clap

### Catch2

- Location: downloaded by CMake in `tests/`
- License: Boost Software License 1.0
- Use in Aizana: test infrastructure only; not part of the Aizana app runtime.
- Upstream: https://github.com/catchorg/Catch2

## Plugin SDK and Platform Notes

### VST3 SDK

- Location: included through JUCE's audio processor format support
- License: Steinberg VST3 License or GPLv3
- Release note: Aizana should avoid using Steinberg VST/VST3 trademarks or
  logos in marketing unless the relevant usage guidelines are followed.
- Local license text:
  `extern/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt`

### Apple SDKs and Audio Units

Aizana uses Apple platform APIs such as CoreAudio, AudioUnit, AVFoundation, and
CoreML through JUCE, Tracktion Engine, and macOS SDKs. These are system/platform
interfaces and are not bundled as Aizana source dependencies.

For public macOS distribution, prefer a notarized direct download over Mac App
Store distribution because GPL/AGPL-style copyleft and app-store terms can be
hard to reconcile.

## AI and Model Weight Notices

### demucs.cpp

- Location: `extern/demucs_cpp`
- License: MIT
- Use in Aizana: local source-separation inference wrapper for Demucs-format
  models.
- Upstream: https://github.com/sevagh/demucs.cpp

### Demucs Code

- Upstream: https://github.com/facebookresearch/demucs
- License: MIT for the Demucs source code.

### Demucs / htdemucs Model Weights

Aizana must not bundle pretrained Demucs model weights in the source repo or
binary release unless the distributor has independently verified the right to do
so.

Aizana' current public-release-safe model is:

- no model weights in the Git repository
- no model weights in the app bundle
- first-use download initiated by the user
- clear UI/distribution notice showing the model source and explaining that the
  download happens between the user and the hosting provider

Current code points at the Hugging Face dataset:

- https://huggingface.co/datasets/Retrobear/demucs.cpp

The dataset card labels the converted weights as MIT and lists the original
Meta/Facebook Research weight origins. Because model weights are separate from
source code and have historically had licensing ambiguity, any distributor that
bundles weights should re-check the current model license before release.

## Not Yet Integrated (add before shipping the plugin generator)

The Text→Faust→CLAP plugin generator (Spike S2, proven; not yet in the app) will
add these and they must be listed here once integrated:

- **Faust compiler** (GRAME) — GPL with the *Faust exception*, which permits the
  compiler's generated output to be relicensed; the bundled Faust runtime/architecture
  headers are permissive/GPL-compatible. Confirm at release.
- **clap-helpers** (https://github.com/free-audio/clap-helpers) — MIT; C++ wrapper
  headers required by Faust's CLAP architecture.
- Faust standard libraries used per template (mostly permissive / STK-style) — verify
  per template that is shipped.

## Local Test Assets

Local songs and generated stems are excluded by `.gitignore`:

- `/*.mp3`
- `/*.wav`
- `/Stems_*/`

Do not include private songs, stems, third-party loops, screenshots, commercial
logos, or generated model weights in public source archives or release zips
unless their rights are documented.

## Release Checklist

Before a public source or binary release:

1. Include `LICENSE.md`, this file, and exact submodule commits.
2. Include source access for the exact binary release.
3. Include notices for JUCE, Tracktion Engine, SoundTouch, ONNX Runtime, Eigen,
   libebur128, CLAP, Catch2 if tests are shipped, and VST3 SDK where applicable.
4. Do not bundle Demucs/htdemucs weights unless their separate license has been
   verified for redistribution.
5. Do not ship ignored local audio material.
6. Avoid implying endorsement by Apple, Fender, PreSonus, Steinberg, Avid,
   Ableton, FabFilter, Meta, Microsoft, or other third parties.
