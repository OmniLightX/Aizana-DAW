# Running Aizana 0.3.0 Alpha from the repository

The current distribution channel is exclusively the GitHub repository.
No DMG or prebuilt app package is offered for 0.3.0.

1. Clone the repository, load the submodules and follow the build steps in
   [README.md](../README.md#build-from-source).
2. The built app is at `build/Aizana_artefacts/Release/Aizana.app`.
3. Open the app there; optionally copy the app you built to Applications.

The build has been verified for Apple Silicon (arm64), macOS 13.3 or later.
A local build has no verified Apple developer identity and is not notarized.
First launch on an independent Mac has not yet been accepted.
Aizana does not disable macOS security features. Apple describes possible
launch warnings at https://support.apple.com/102445.

Back up important sessions before opening them. Microphone permission and audio
settings are managed by macOS and Aizana, respectively. Additional plugins,
Faust and AI CLIs are installed separately; see README.md.
