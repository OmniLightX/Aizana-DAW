# demucs.cpp (vendored)

Source-separation inference library for Demucs v4 hybrid-transformer models.

- Upstream: https://github.com/sevagh/demucs.cpp
- Vendored commit: `f1206e9adeea103aef4a636b9e62297cf1f8e34e` (2026-06-12)
- License: MIT (see LICENSE — kept verbatim)
- Contents: `src/` library only + `threaded_inference.hpp` from upstream
  `cli-apps/` (multi-threaded segmentation driver). The CLI apps, libnyquist,
  googletest and the Python conversion scripts are NOT vendored — Aizana does
  its own audio I/O via JUCE.
- Dependency: Eigen (header-only), pinned via FetchContent in the root
  CMakeLists.txt to the same commit upstream vendors (`dd8c71e6…`).
- Models: ggml weights (htdemucs-6s, htdemucs_ft_*) are downloaded at runtime,
  never bundled. NOTE: verify the WEIGHTS license before public release —
  the code is MIT, the model weights are licensed separately.
- Constraint: inference operates at 44.1 kHz stereo only — callers must
  resample (see src/ai/StemSeparationEngine).
