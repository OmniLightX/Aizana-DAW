#include "MicrophonePermission.h"
#include <juce_core/juce_core.h>

#if defined(__APPLE__) && ! TARGET_OS_IPHONE
#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>

void requestMicrophonePermission (std::function<void (bool)> callback)
{
    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];

    juce::String statusStr;
    switch (status)
    {
        case AVAuthorizationStatusNotDetermined:  statusStr = "NotDetermined"; break;
        case AVAuthorizationStatusRestricted:     statusStr = "Restricted";    break;
        case AVAuthorizationStatusDenied:         statusStr = "Denied";        break;
        case AVAuthorizationStatusAuthorized:     statusStr = "Authorized";    break;
        default:                                  statusStr = "Unknown";       break;
    }
    NSLog (@"[Aizana] Microphone permission status: %@", (__bridge NSString*) statusStr.toCFString());

    if (status == AVAuthorizationStatusAuthorized)
    {
        NSLog (@"[Aizana] Already authorized — proceeding");
        dispatch_async (dispatch_get_main_queue(), ^{ callback (true); });
        return;
    }

    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted)
    {
        NSLog (@"[Aizana] Microphone DENIED — user must enable in System Settings");
        dispatch_async (dispatch_get_main_queue(), ^{ callback (false); });
        return;
    }

    // NotDetermined — show the system dialog.
    NSLog (@"[Aizana] Requesting microphone permission via AVCaptureDevice...");
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted)
    {
        NSLog (@"[Aizana] Microphone permission result: %@", granted ? @"GRANTED" : @"DENIED");
        dispatch_async (dispatch_get_main_queue(), ^{ callback (granted); });
    }];
}

#else

void requestMicrophonePermission (std::function<void (bool)> callback)
{
    callback (true);
}

#endif
