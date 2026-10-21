# Distribution and source disclosure

Aizana is available as a GitHub source repository, with
local builds from source. No 0.3.0 DMG or prebuilt app release is offered. The
binary-distribution requirements below apply if binaries are distributed
later; they do not announce such a release.

Aizana's own source remains GPL-3.0-or-later. JUCE's pinned modules are used under
AGPLv3; Tracktion under GPLv3-or-later. GPLv3 and AGPLv3 section 13 permit the
combination while preserving the respective terms. Include both license texts.

For each binary distribution provide, at the same download location and without
an additional charge, the complete corresponding source for that exact version:
Aizana source, pinned dependency source, modified dependency patches, generator
DSP/architecture/header inputs and scripts needed to build and install it.
A GitHub auto-generated source ZIP alone does not include submodules. Provide a
complete source archive, or an equivalent fully accessible source distribution.
A private repository link is not source access for a public recipient.

Retain copyright notices and modification notices. Include COPYING,
COPYING.AGPL, LICENSE.md, THIRD_PARTY_NOTICES.md and the licenses directory in
the binary distribution. Recipients may modify and redistribute under these
terms; do not add a conflicting no-redistribution or no-reverse-engineering EULA.
Where modified AGPL-covered software supports remote user interaction, section
13 requires a prominent corresponding-source offer to those users.

Audio created with the DAW does not become
GPL/AGPL merely because it was processed with this application. Third-party
samples, plugins, models and generated DSP may have separate terms.

No proprietary plugins or model weights are bundled. The optional Faust compiler
is installed by the user. Its LGPL license does not automatically apply to its
output; the input libraries and architecture files determine output obligations.
Bundled generated CLAPs retain notices and are accompanied by their source inputs.
Do not represent output licensing as unrestricted without checking those inputs.

Primary references, checked 2026-10-18:
- https://www.gnu.org/licenses/gpl-3.0.html (sections 6 and 13)
- https://www.gnu.org/licenses/agpl-3.0.html (section 13)
- https://juce.com/get-juce/
- https://faustdoc.grame.fr/manual/faq/#what-is-the-situation-about-faust-compiler-licence-and-the-deployed-code

Exact dependency versions and their local license texts control this release,
not the license of a newer upstream version. In particular the bundled VST3 SDK
still offers the GPLv3 alternative in its pinned LICENSE.txt.


## Optional stem model data

The source-only app distribution does not include pretrained weights. The optional
Mega 53 ONNX conversion is separate model data, not a prebuilt application or
installer. Its own MIT notices, exact source/changes and SHA-256 accompany it;
full text is also embedded in the ONNX metadata. See OPEN_STEM_RELEASE.md for
provenance, accepted limitations, trainer warranty limitation and download status.
Anonymous first-use model download has not yet been accepted; see
[the current test status](../STATUS.md).

### MP3 encoder source

The static LAME 3.100 encoder is built from the unmodified, hash-verified
`extern/lame/lame-3.100.tar.gz`. Include that archive and `cmake/Lame.cmake`
in the corresponding-source package, together with the matching Aizana source
and build instructions so recipients can modify LAME and relink Aizana.
The existing app packaging includes `licenses/LAME/` and the third-party notice.
See [the LAME provenance record](../extern/lame/README.md).
