# Lizenzen und Nutzung

## Nutzung und Weitergabe

Aizana wird vom Projekt kostenlos als Quellcode über GitHub bereitgestellt.
Berufliche Nutzung, auch für bezahlte Musikproduktionen, ist erlaubt. Die
Softwarelizenzen gelten unabhängig davon, ob das Projekt Einnahmen erzielt.
Es wird kein Zusatz „nur nichtkommerziell“ eingeführt. GPL/AGPL erlauben auch
kostenpflichtige Weitergabe unter ihren Bedingungen; unsere kostenlose Abgabe
verbietet Dritten daher nicht pauschal, eigene Distributionen zu verkaufen.

Eigener Code bleibt GPL-3.0-or-later. JUCE wird unter dessen AGPLv3-Alternative,
Tracktion unter GPLv3-or-later verwendet. GPLv3 und AGPLv3 erlauben in Abschnitt
13 die Kombination, ohne die jeweiligen Komponententexte zu ersetzen. Eine
pauschale MIT-Lizenz für das Gesamtprojekt wäre kein Ersatz für diese Pflichten.

## Komponenten

Die vollständigen Lizenz- und Urheberhinweise stehen in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) und im Verzeichnis `licenses/`.

| Komponente | Gewählter Weg / Einordnung | Für die Quellcode-Verteilung |
|---|---|---|
| Aizana-Code | GPL-3.0-or-later | LICENSE.md, COPYING, Urheber- und Änderungsvermerke erhalten |
| JUCE 8 | AGPLv3 statt kommerzieller JUCE-Lizenz | COPYING.AGPL und JUCE-/Unterkomponenten-Hinweise erhalten; AGPL-Bedingungen einhalten |
| Tracktion Engine | GPLv3-or-later statt kommerzieller Lizenz | Exakten Submodulstand, Hinweise und eigenen Recording-Patch bereitstellen |
| SoundTouch in Tracktion | LGPL-2.1-or-later | Lizenzhinweise und Quellzugang erhalten |
| demucs.cpp, libebur128, CLAP, clap-helpers | MIT | Copyright- und Lizenztexte erhalten |
| ONNX Runtime | MIT, weitere Upstream-Hinweise | Version und Bezug dokumentieren; ThirdPartyNotices bei Weitergabe beachten |
| Eigen | MPL-2.0 und enthaltene kompatible Dritttexte | Dateibezogene Lizenz-/Quellpflichten und zusätzliche Hinweise erhalten |
| Catch2 | BSL-1.0, Testabhängigkeit | Lizenztext erhalten |
| Faust | Extern installierter LGPL-Compiler | Compilerlizenz nicht mit Lizenz generierter Ausgabe verwechseln; DSP-/Architektur-/Headerbedingungen beachten |
| VST3 SDK im festgelegten JUCE-Stand | GPLv3-Alternative | SDK-Lizenztext erhalten; keine Markenfreigabe daraus ableiten |
| Apple SDKs | Separat bezogene Plattformwerkzeuge | Nicht als eigenen offenen Quellcode oder mit eigener Lizenz ausgeben |

Optionale Drittanbieter-Dienste, Plugins und zusätzlich geladene Modelle
unterliegen ihren jeweiligen Nutzungs- und Lizenzbedingungen.

## Quellcode und Abhängigkeiten

Ein automatisch erzeugtes GitHub-ZIP enthält keine Submodulquellen. Für den
vollständigen Checkout ist die dokumentierte Submodulinitialisierung nötig.
Für eine spätere Binärverteilung gelten zusätzlich die passenden Pflichten zur
Bereitstellung des vollständigen korrespondierenden Quellcodes, siehe DISTRIBUTION.md.

Eigene Songs werden nicht allein durch Bearbeitung mit Aizana GPL/AGPL-lizenziert.
Rechte an verwendeten Samples, Aufnahmen und Plugins bleiben davon unabhängig.

## Weiterführende Lizenztexte

- [Projektlizenz](../LICENSE.md)
- [GPLv3](../COPYING) und [AGPLv3](../COPYING.AGPL)
- [Vollständige Komponentenhinweise](../THIRD_PARTY_NOTICES.md)
- [Quellcode-Bereitstellung bei Weitergabe](DISTRIBUTION.md)
- [Modellherkunft und Modelllizenz](OPEN_STEM_RELEASE.md)
