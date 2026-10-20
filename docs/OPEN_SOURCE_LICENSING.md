# Licensing and use

## Use and redistribution

The project provides Aizana free of charge as source code through GitHub.
Professional use, including paid music production, is permitted. The software
licenses apply regardless of whether the project generates revenue.
No “non-commercial use only” restriction is introduced. GPL/AGPL also permit
paid redistribution under their terms; our free distribution therefore does
not impose a blanket ban on third parties selling their own distributions.

The project's own code remains GPL-3.0-or-later. JUCE is used under its AGPLv3
alternative, and Tracktion under GPLv3-or-later. Section 13 of GPLv3 and AGPLv3
permits the combination without replacing the respective component texts.
A blanket MIT license for the whole project would not replace these obligations.

## Components

The full license and copyright notices are in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and the `licenses/` directory.

| Component | Selected route / classification | For source distribution |
|---|---|---|
| Aizana code | GPL-3.0-or-later | Retain LICENSE.md, COPYING, copyright and modification notices |
| JUCE 8 | AGPLv3 instead of a commercial JUCE license | Retain COPYING.AGPL and JUCE/subcomponent notices; comply with AGPL conditions |
| Tracktion Engine | GPLv3-or-later instead of a commercial license | Provide the exact submodule revision, notices and the project's recording patch |
| SoundTouch in Tracktion | LGPL-2.1-or-later | Retain license notices and source access |
| demucs.cpp, libebur128, CLAP, clap-helpers | MIT | Retain copyright and license texts |
| ONNX Runtime | MIT, additional upstream notices | Document version and source; observe ThirdPartyNotices when redistributing |
| Eigen | MPL-2.0 and included compatible third-party texts | Preserve file-level license/source obligations and additional notices |
| Catch2 | BSL-1.0, test dependency | Retain license text |
| Faust | Externally installed LGPL compiler | Do not confuse the compiler license with the license of generated output; observe DSP/architecture/header terms |
| VST3 SDK in the pinned JUCE revision | GPLv3 alternative | Retain SDK license text; do not infer trademark permission from it |
| Apple SDKs | Separately obtained platform tools | Do not present them as the project's own open source or under the project's own license |

Optional third-party services, plugins and additionally downloaded models
are subject to their respective terms of use and licenses.

## Source code and dependencies

An automatically generated GitHub ZIP does not contain submodule sources.
A complete checkout requires the documented submodule initialization.
Any future binary distribution is also subject to the applicable obligations
to provide complete corresponding source; see DISTRIBUTION.md.

Your own songs do not become GPL/AGPL-licensed merely by being edited with Aizana.
Rights to the samples, recordings and plugins used remain separate.

## Further license texts

- [Project license](../LICENSE.md)
- [GPLv3](../COPYING) and [AGPLv3](../COPYING.AGPL)
- [Full component notices](../THIRD_PARTY_NOTICES.md)
- [Source provision when redistributing](DISTRIBUTION.md)
- [Model provenance and model license](OPEN_STEM_RELEASE.md)
