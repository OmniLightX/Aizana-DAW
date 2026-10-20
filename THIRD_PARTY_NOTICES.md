# Third-Party Notices

This file summarizes third-party components used by Aizana and the release
obligations that matter for public source or binary distribution.

When distributing a binary, include this notice, the project license, the exact source for the
released commit, and the applicable license texts and notices for each bundled
dependency. License texts do not replace corresponding source where a component's
license requires it; follow `docs/DISTRIBUTION.md` for source delivery.

## Project License

Aizana source code is licensed under GPL-3.0-or-later. Aizana combines with
third-party libraries under their own licenses. Public releases must satisfy the
licenses of the combined work, including JUCE and Tracktion Engine.

## Runtime and Framework Dependencies

### LAME 3.100

- Upstream: https://lame.sourceforge.io/
- License: LGPL-2.0-or-later (library source headers; original LGPL v2 in COPYING).
- Exact unmodified source: `extern/lame/lame-3.100.tar.gz`; provenance and SHA-256
  in `extern/lame/README.md`. Original notices: `licenses/LAME/`.
- Built as a separate static encoder library. No runtime LAME installation is needed.
- Binary distributions must include the corresponding LAME source and the
  matching application source/object material and build scripts required to
  modify the library and relink the executable, as required by LGPL section 6.
  Include the archive in the corresponding-source package; a link alone is insufficient.

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

### Tracktion transitive components

The pinned engine also contains these components. Header notices remain in the
submodule; selected embedded notices are additionally preserved in
`licenses/Tracktion/embedded/`. Inclusion here inventories source, not a claim
that every platform-specific component executes in the macOS app.

| Component | Upstream license | Evidence |
|---|---|---|
| Airwindows | MIT, Chris Johnson | `licenses/Tracktion/Airwindows/LICENSE` and origin record |
| CHOC | ISC, additional notices where specified | `licenses/Tracktion/3rd_party/choc/LICENSE.md` |
| crill | BSL-1.0 | `licenses/Tracktion/3rd_party/crill/LICENSE.txt` |
| farbot | MIT | `licenses/Tracktion/tracktion_graph/3rd_party/farbot/LICENSE` |
| rpmalloc | Public-domain dedication | embedded rpmalloc header notice |
| ConcurrentQueue | Simplified BSD / Boost option | embedded ConcurrentQueue header notice |
| tl::expected | CC0-1.0 | `licenses/Tracktion/expected/COPYING` and header |
| libsamplerate | BSD-2-Clause | `licenses/Tracktion/libsamplerate/COPYING` and header |
| NanoRange | BSL-1.0 | embedded header; full BSL text also in crill/Catch2 notices |
| rigtorp MPMCQueue | MIT | embedded MPMCQueue header notice |
| magic_enum | MIT | embedded magic_enum header notice |
| doctest 2.4.11 | MIT; acknowledged BSL-derived portions | `licenses/Tracktion/doctest/LICENSE.txt`, header and BSL text |

Upstream license additions include retrieval origins. They supplement, rather
than replace, copyrights and modification notices in the pinned engine.

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

The supplied ThirdPartyNotices file is a broad upstream aggregation, including
components for configurations/platforms other than this macOS build. Its listing
of a component alone does not prove that component is linked here. Aizana does
not relabel all entries as MIT or redistribute the ONNX binary in its Git tree;
the configured build downloads the separately licensed upstream distribution.

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

Current distribution is source through GitHub, with local builds using Apple
development tools. Apple SDKs remain separately obtained platform requirements.

## AI and Model Weight Notices

### MVSep Mega 53 Stems v1 (optional model data)

- Original trainer: Roman Solovyev (ZFTurbo); model weights explicitly released
  under MIT: https://github.com/ZFTurbo/Music-Source-Separation-Training/issues/245
- Original checkpoint and configuration: upstream release v1.0.21.
- Architecture attribution: Phil Wang, BS-RoFormer (MIT).
- Full notices: `licenses/Mega53/LICENSE.Mega53` and
  `licenses/Mega53/LICENSE.BS-RoFormer`; also embedded in the converted ONNX file.
- Aizana selects the original drums, bass, vocal, guitar and piano heads, without
  retraining. Native STFT/ISTFT surround the exported mask network. The sixth
  output, Other, is the input minus the five estimates. Four-category mode folds
  guitar and piano into Other. This is a conversion, not a newly trained model.
- Exact upstream sources, hashes, changes and the final artifact are recorded in
  `docs/OPEN_STEM_RELEASE.md`. Metadata packaging and input-hash checks are in
  `scripts/package_mega53_model.py`.
- Model data is downloaded only with user consent, then size/SHA-256 verified.
  No weights or private test audio are tracked in this source repository.
- The trainer does not warrant rights in all training recordings or provide
  indemnification. MIT permission is not a blanket training-data clearance.

### Legacy demucs.cpp source

`extern/demucs_cpp` remains MIT-licensed archival source, with its notice retained
at `licenses/demucs.cpp/LICENSE`. The active separation backend does not load or
request Demucs/htdemucs weights. Earlier research about their ambiguous weight
license remains historical evidence, not the active provider description.

## Integrated plugin generator

The Faust-to-CLAP generator is part of this release. Faust 2.85.5 is a build-time
and optional runtime tool installed by the user; its compiler binary is not
bundled. The installed compiler is LGPL-2.1-or-later (see licenses/Faust-COPYING.txt).
The license of generated output depends on the input DSP libraries and
architecture files, not automatically on the compiler license.

Bundled templates are built from generator/templates, the Faust CLAP architecture,
Faust headers/standard libraries, and CLAP/clap-helpers. Their source inputs and
notices must accompany the corresponding-source distribution. Some unmodified
Faust architecture headers include their own special exception; retain it.
The CLAP backend itself does not carry that exception in its file header; do
not assume every architecture file grants identical output permissions. The
source-only distribution obtains Faust separately. Generated output must retain
the actual applicable notices. The current reverb uses stereo_freeverb; generated
metadata identifies MIT-style STK-4.3 filter code and LGPL-with-exception maths.
Other functions in the same library have different terms and must not be
automatically treated as having those same permissions.
Do not apply a blanket claim that all generated plugins have no license obligations.

- CLAP generator API: a47f6badb49d948fd009998f28309cdab78979c9 (MIT).
- clap-helpers: 629ae7526740e3d0ee4d64c230df183ecdb2ff5a (MIT).
- Host CLAP API remains pinned to 1.2.0 (MIT).
- Faust: https://faustdoc.grame.fr/manual/faq/

## Source and binary delivery

Complete collected license texts are in licenses/. Include them in the app's
Resources/Licenses directory and in the source archive. A private GitHub URL
alone is not a public corresponding-source offer. The source archive must
include submodules and the recording patch, not only the top-level checkout.
See docs/DISTRIBUTION.md and docs/RELEASE_READINESS.md.
