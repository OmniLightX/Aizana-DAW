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

## Status in Aizana

This is retained legacy source. CMake still builds `demucs_inference` and lists
it as a link dependency of the app and tests, with Eigen as its header dependency.
It is not the active stem-separation backend.

Aizana uses MVSep Mega 53 through `RoFormerStemBackend` and ONNX Runtime. It does
not download or load htdemucs/ggml model weights. The active model's provenance,
conversion and MIT notices are documented in
[the model report](../../docs/OPEN_STEM_RELEASE.md).

The MIT notice for this retained source and Eigen's applicable notices remain
necessary while these sources and dependencies are included. They do not describe
the license of the separately downloaded Mega 53 model.
