# Publication review follow-up

24 October 2026. The external review examined `21798bc1`; implementation was
rechecked against `98dc4969`, including the recording, waveform, CoreAudio,
channel-removal and documentation changes committed since that review.
The external report itself is not part of this source distribution.

| Review item | Current disposition |
| --- | --- |
| Generated plugin display names | Encoded as C++ literals; special characters and UTF-8 tested through Clang and real hosted CLAP output. |
| Compiler timeout | Nonblocking output drain, bounded diagnostics, shared Faust/Clang deadline, process-group termination, temporary output before successful publication. |
| Native code during scanning | Already documented in both manuals and the MCP guide in `98dc4969`; retained and linked from SECURITY.md. |
| Historic personal attribution and internal notes | Completed after explicit authorization: four branches and three tags updated with per-ref leases. All 449 commit trees compared; only the original-author license attribution and twelve obsolete work journals changed. Code, tests, dependency pins and vendor notices are identical. Backup retained privately. |
| Security reporting | SECURITY.md added. Private GitHub reporting requires a public repository and separate activation; no live reporting channel is claimed before activation. |
| Clean CI | macOS workflow activated in .github/workflows/build.yml with pinned action revisions, read-only permissions, pinned source submodules, Release build, patch checks and CTest. Hosted run [40039334331](https://github.com/OmniLightX/Aizana-DAW/actions/runs/40039334331) passed. Earlier compiler compatibility and recording-test timing failures were corrected; a separate intermittent Warp crash remains under investigation, as recorded in STATUS.md. |

See [test status](../STATUS.md) for observed results. Previous hardware and
listening checks remain separate; this follow-up does not turn pending checks
into passing results. No change to repository visibility, license ownership or
third-party notices is included. A rewritten Git history would not by itself
remove GitHub-retained objects or copies held elsewhere.
