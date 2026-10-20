# Local stem separation: model, implementation and tests

The distribution is source-only, built by users. No installer or prebuilt app
is published. This document describes the model, conversion and test coverage.

## What the app actually uses

The default KI-Mix uses Aizana's analysis and mixing rules, not pretrained foreign
mixing weights. Optional user-supplied models and external AI CLI accounts remain
separate. The local stem backend uses MVSep Mega 53 Stems v1, trained by Roman
Solovyev (ZFTurbo), with five original mask heads. No Aizana training took place.
The five chosen original heads are drums (15), bass (4), vocal (49), guitar (20)
and piano (33); Other is the original waveform minus those estimates. Four-output
mode folds guitar and piano into Other. Legacy enum names do not claim a speed
advantage or a separate fine-tuned model.

The native worker runs ONNX Runtime, FFT2048/hop512 and overlap-add processing.
No Python runtime is needed in the consumer app. Processing is local; the initial
model request contacts GitHub only after consent. Audio is not uploaded by this
backend. This statement does not cover users' external agents or plugins.

## Permission, attribution and conversion

The original trainer expressly grants MIT permission for the 53-stem weights,
including conversion and redistribution:
https://github.com/ZFTurbo/Music-Source-Separation-Training/issues/245

Checkpoint/configuration source:
https://github.com/ZFTurbo/Music-Source-Separation-Training/releases/tag/v1.0.21

Full MIT notices are retained in `licenses/Mega53/`, `docs/models/` and embedded
inside the downloadable ONNX model. Architecture attribution is Phil Wang;
weight/repository attribution is Roman Solovyev. The conversion selects five
original independent heads without retraining, exports the mask graph with ONNX
opset 17, strips exporter doc strings and adds license/provenance metadata.
Native code performs STFT/ISTFT and derives the residual. Reproduction tools and
pinned inputs are in `docs/research/mega53/` and `scripts/package_mega53_model.py`.
Conversion/evaluation Python packages are developer tools, not app dependencies
or redistributed Python implementations. Spleeter was compared, not integrated.

Final artifact: `mega53-five-masks-v1.onnx`, 355012879 bytes.
SHA-256: `d546b9780159ee8e54c3cf7c0446a4204b8793e363941122b4c115089d369cc2`.
The raw research export has a different checksum because it lacks the final
embedded notices/metadata. Exact upstream and converted hashes are retained in
`docs/research/mega53/candidate-manifest.json`.

For this integration, Aizana uses converted pretrained weights; it does not
redistribute the original training recordings or train a replacement model.
The MIT redistribution condition is to retain the copyright and permission
notice; we preserve the complete license, including its warranty disclaimer.
Source URLs, version identifiers, conversion details and checksums additionally
make the artifact traceable; these are not extra obligations imposed by MIT.
The upstream training-data caveat below is a disclosure of the trainer's stated
warranty limits, not an additional license condition or a finding of infringement.

MIT permission covers the supplied model under its stated terms. The trainer
expressly provides no warranty or indemnification concerning rights in all
training recordings. Aizana does not redistribute the training recordings. The upstream
warranty limitations are retained with the model license.

## Source-license conditions

Aizana's own code remains GPL-3.0-or-later. JUCE's selected open-source route is
AGPLv3; Tracktion's is GPL; other dependencies retain their MIT/BSD/MPL and other
applicable notices. The whole project must not be advertised as MIT-only.
`LICENSE.md`, `COPYING`, `COPYING.AGPL`, `THIRD_PARTY_NOTICES.md`, `licenses/`,
pinned dependencies, build scripts and the Tracktion recording modification patch
form the source-delivery record. The combined GPL/AGPL covered work retains the
AGPL conditions where applicable; the top-level own-code grant does not override
JUCE. Redistribution of a built version requires its corresponding source under
the applicable licenses, not just a private URL or unrelated latest branch.

AGPL section 13 concerns covered modified versions with remote user interaction.
There is no hosted Aizana service in this release scope; MCP is stdio and the live
bridge binds to loopback. Local inference alone is not the legal test. Any future
network service must be checked separately and supply the required prominent
source offer where applicable. User-installed commercial plugins and external
accounts are not redistributed under Aizana's license.

## Verified tests and accepted limitations

- Complete engine suite: 431 passed, 11 skipped, 49622 assertions. The shared
  debug harness emitted JUCE teardown diagnostics; this is not claimed to be a
  diagnostic-free run.
- End-to-end native engine test: both four/six output modes passed; float WAVs,
  aligned length, non-overwrite and cancellation cleanup passed (69 assertions).
- Public full Release suite: 432 passed, 11 skipped; 49673 assertions passed.
- Public Release app and test targets built successfully. Four recording-patch
  regressions passed. Selected bundle privacy/dependency/license audit passed
  across eight Mach-O files; full source license hashes matched ten direct
  dependency records and 71 collected notices.
- Public Release real-model tests also passed: 125 assertions across native
  reference parity and complete engine output/cancellation tests.
- Exact final packaged model: native six-waveform parity passed, 56 assertions;
  maximum allowed per-sample error 0.0002, RMS error 0.00001 and reconstructed
  mixture error 0.000001 against the independent reference.
- Short stereo lengths and spectral boundaries, missing models and early cancel
  are covered by regression tests. The app Debug build passed.
- Independent read-only review found no verified new blocker or warning; its
  sandbox prevented two temporary-directory test executions, and it did not
  independently rerun full-model or GUI tests.
- The authorized real multitrack test improved guitar by about 9.5–9.9 dB SI-SDR,
  but worsened its particular synthesized piano-like timbre. This timbre limitation is retained in the evaluation results.
- A separate acoustic-piano test improved mixed-piano SI-SDR by 20.54 dB with
  Mega versus 5.59 dB with Spleeter. Solo piano measured 37.30 dB with Mega.
  These are numerical comparisons, not a claim of completed listening acceptance.
- Acoustic reference: Alexander Holm, Salamander Grand Piano V3, CC-BY-3.0,
  sfzinstruments/SalamanderGrandPiano commit
  `3382bf9496bba2486f5ab0de55a264d1dfc38404` (A3/C4/D#4/F#4 velocity 8).
  No reference audio or private user material is distributed.
- Separation artifacts, instrument bleed and weak/synthetic keyboard timbres remain
  normal model limitations. Below-threshold outputs may be omitted. Other is a
  residual rather than an independently trained category.

## Model delivery and verification

The optional model is separate data, accompanied by `MODEL_LICENSES.txt` and
`SHA256SUMS`. The model contains 355012879 bytes with SHA-256
`d546b9780159ee8e54c3cf7c0446a4204b8793e363941122b4c115089d369cc2`.
On 20 October all three assets were fully downloaded with authenticated access
and matched the original files byte for byte. The Release build with the current
model URL and the bundle audit passed.

Anonymous download and first-use download through the app have not yet been
accepted. Authenticated transfer verification does not establish that path.

Six-category separation, preview/import and single-step import Undo/Redo passed.
Cancellation removed the progress panel without importing tracks or changing
previous output files. Session-switch cancellation also passed: no late result
entered the new session. The test hook disables network downloads, so these
checks do not validate first-use network delivery.

Current test coverage and remaining limits: [STATUS.md](../STATUS.md).
