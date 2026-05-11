#pragma once
#include <functional>

// Request microphone access via AVFoundation (macOS-native, shows system dialog).
// On non-macOS platforms the callback fires immediately with granted = true.
// The callback is always delivered on the main thread.
void requestMicrophonePermission (std::function<void (bool granted)> callback);
