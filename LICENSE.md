# Aizana License

Copyright (C) 2026 Aizanagi and the Aizana contributors.

Aizanagi is the original project author’s chosen public attribution name,
not a claim that an incorporated company exists. Contributors retain copyright
in their respective contributions; this collective notice does not assign rights
or replace third-party copyright notices.

Aizana's own source code is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your option)
any later version.

Aizana is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

**The full, verbatim text of the GNU General Public License v3.0 is included in
the [`COPYING`](COPYING) file in this repository.** If you did not receive it,
see <https://www.gnu.org/licenses/gpl-3.0.txt>.

---

## Rationale: Copyleft Compatibility

Aizana uses JUCE 8 (https://github.com/juce-framework/JUCE), which is
dual-licensed under AGPLv3 and a commercial JUCE license.

Aizana also uses Tracktion Engine
(https://github.com/Tracktion/tracktion_engine), which is dual-licensed under
GPL3-or-later and a commercial Tracktion Engine license.

The open-source distribution uses the GPLv3/AGPLv3 combination permitted by
section 13 of both licenses. The respective component terms remain applicable;
this is not a GPL-only binary. Include COPYING and COPYING.AGPL and provide the
complete corresponding source for the exact distributed binary. See
docs/DISTRIBUTION.md for the release checklist.

The commercial alternatives offered by these vendors are not selected for this
release. Their mention does not impose a purchase requirement on this open-source
distribution. Original upstream license texts, including their alternative
licensing offers, are preserved unchanged.

For the detailed dependency and model-weight notices, see
`THIRD_PARTY_NOTICES.md`.

## Third-Party License Summary

- **JUCE 8** — AGPLv3 (selected open-source route)
- **Tracktion Engine** — GPL3-or-later (selected open-source route)
- **LAME 3.100** — LGPL-2.0-or-later; original source and provenance in
  [`extern/lame/README.md`](extern/lame/README.md), notices in `licenses/LAME/`,
  static-link redistribution requirements in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md#lame-3100)
- **SoundTouch** — LGPL-2.1-or-later, bundled inside Tracktion Engine
- **demucs.cpp (retained legacy source/build dependency)** — MIT License
- **ONNX Runtime** — MIT License
- **Eigen** — primarily MPL-2.0
- **libebur128** — MIT License
- **CLAP C API** — MIT License
- **Catch2** — BSL-1.0 (Boost Software License, test infrastructure only)

- **Optional MVSep Mega 53 model weights and BS-RoFormer architecture** — MIT;
  exact provenance, conversion and separate download terms are documented in
  `docs/OPEN_STEM_RELEASE.md` and the full notices in `licenses/Mega53/`.

## Project name and artwork

Permission to use the Aizana name, project logo and README banner is specified
in [Name and logo permissions](docs/BRANDING.md). This includes clearly labelled
community versions and does not change the software licenses above.
