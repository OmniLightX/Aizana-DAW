# MP3 export

Mixdown and stems render offline through Tracktion, then encode a temporary
32-bit stereo WAV with the bundled LAME 3.100 library. The live playback graph
is detached for rendering. Encoding runs on a worker thread while the message
thread handles cancellation. No external encoder process or installation is used.

MP3 settings: 44.1 or 48 kHz; 128, 192, 256 or 320 kbit/s CBR. The default is
320 kbit/s. LAME writes delay/padding information for gapless decoding. The
rendered range remains zero to the last clip, matching the existing export;
this change does not introduce an additional reverb tail or loudness normalization.

Each output is rendered into a unique sibling temporary file. The PCM header,
channel count, sample rate and frame count are checked before encoding. Render,
encoder, flush and publication errors return a failure. Cancellation and errors
leave the previous destination intact. Only the completed file replaces it.
Stem export stops on its first error; previously completed stems remain. Duplicate
track names receive numeric suffixes. Temporary files are removed by RAII.

The library's exact source, checksum and distribution requirements are documented
in [the provenance record](../extern/lame/README.md). License texts are packaged
with the existing app license-copy rule.

## Reproducing the checks

Build `engine_tests`, then run it with the Catch2 filter `[export]`. The tests
cover the PCM reference signal, MP3 rates/bitrates, decoded duration and stereo
content, stems, duplicate names, invalid parameters and paths, encoder input/write
errors, and cancellation with preservation of an existing file.

Set `AIZANA_EXPORT_TEST_ARTIFACTS` to an absolute output directory to retain the
eight MP3 combinations and two AIFF references from the signal test. FFmpeg is
optional and used only for independent verification, never by the application.
For each retained MP3, `ffprobe -show_streams` should report MP3, two channels,
and the sample rate and bitrate in its filename. Decoding to stereo float32 PCM
with `ffmpeg -i INPUT.mp3 -f f32le OUTPUT.raw` should produce exactly 44,100 or
48,000 frames (eight bytes per stereo frame), corresponding to the one-second
reference after applying delay/padding metadata.

The native dialog and listening checks are listed separately in
[LIVE_TEST_CHECKLIST.md](LIVE_TEST_CHECKLIST.md). Automated signal tests do not
replace the recording/mixing/mastering practice test.
