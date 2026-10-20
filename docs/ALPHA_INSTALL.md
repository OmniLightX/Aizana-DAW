# Aizana 0.3.0 Alpha aus dem Repository starten

Der aktuelle Vertriebsweg ist ausschließlich das GitHub-Repository. Es wird
kein DMG und kein fertiges App-Paket für 0.3.0 angeboten.

1. Repository klonen, Submodule laden und die Build-Schritte in
   [README.md](../README.md#aus-dem-quellcode-bauen) ausführen.
2. Die erzeugte App liegt unter `build/Aizana_artefacts/Release/Aizana.app`.
3. Die App dort öffnen; optional die selbst gebaute App nach Programme kopieren.

Geprüft ist der Build für Apple Silicon (arm64), macOS 13.3 oder neuer. Ein
lokaler Build hat keine bestätigte Apple-Entwickleridentität und ist nicht
notarisiert. Der Erststart auf einem unabhängigen Mac ist noch nicht abgenommen.
Aizana deaktiviert keine macOS-Schutzfunktionen. Apple beschreibt mögliche
Startwarnungen unter https://support.apple.com/102445.

Vor dem Öffnen wichtiger Sessions eine Kopie sichern. Mikrofonfreigabe und
Audioeinstellungen werden durch macOS bzw. Aizana verwaltet. Zusätzliche Plugins,
Faust und KI-CLIs werden separat installiert; siehe README.md.
