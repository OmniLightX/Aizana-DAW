"""CoreAudio restart regression: exercise the actual patched callback bodies."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'extern/JUCE/modules/juce_audio_devices/native/juce_CoreAudio_mac.cpp'

def body(text, signature):
    start = text.index('{', text.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

class CoreAudioRestartTests(unittest.TestCase):
    def test_patch_applies_once_and_preserves_unknown_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for folder in ('cmake', 'patches', 'extern/JUCE/modules/juce_audio_devices/native'):
                (root / folder).mkdir(parents=True)
            for path in ('cmake/ApplyJuceCoreAudioPatch.cmake', 'patches/juce-coreaudio-async-restart.patch'):
                shutil.copyfile(ROOT / path, root / path)
            target = root / 'extern/JUCE/modules/juce_audio_devices/native/juce_CoreAudio_mac.cpp'
            target.write_bytes(subprocess.check_output(['git', 'show',
                'HEAD:modules/juce_audio_devices/native/juce_CoreAudio_mac.cpp'], cwd=ROOT / 'extern/JUCE'))
            command = ['cmake', '-P', str(root / 'cmake/ApplyJuceCoreAudioPatch.cmake')]
            for _ in range(2):
                result = subprocess.run(command, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(target.read_bytes(), SOURCE.read_bytes())
            target.write_bytes(target.read_bytes() + b'// local change\n')
            edited = target.read_bytes()
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(target.read_bytes(), edited)

    def test_notification_defers_shutdown_and_closed_device_stays_closed(self):
        source = SOURCE.read_text().split('class AudioIODeviceCombiner final')[1]
        async_body = body(source, 'void restartAsync() override')
        timer_body = body(source, 'void timerCallback() override')
        # Compile the production callback bodies with a small fake device so the
        # notification path and cancellation behavior run without real hardware.
        program = '''
#include <cassert>
struct CriticalSection {};
struct ScopedLock { ScopedLock(CriticalSection&) {} };
struct AudioIODeviceCallback {};
struct Device {
 CriticalSection closeLock, callbackLock;
 bool active=true; int closes=0, restarts=0, timers=0;
 AudioIODeviceCallback client; AudioIODeviceCallback* callback=&client;
 AudioIODeviceCallback* previousCallback=nullptr;
 void close() { ++closes; active=false; }
 void startTimer(int) { ++timers; }
 void stopTimer() { timers=0; }
 void restart(AudioIODeviceCallback* cb) { assert(cb==callback); ++restarts; }
 void restartAsync() ASYNC
 void timerCallback() TIMER
};
int main() {
 Device d;
 d.restartAsync();
 assert(d.closes==0 && d.restarts==0 && d.timers==1);
 d.timerCallback(); assert(d.restarts==1 && d.timers==0);
 d.restartAsync(); d.active=false; d.timerCallback();
 assert(d.restarts==1);
}
'''.replace('ASYNC', async_body).replace('TIMER', timer_body)
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp) / 'test.cpp'
            cpp.write_text(program)
            exe = pathlib.Path(tmp) / 'test'
            subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(exe)], check=True)
            self.assertEqual(subprocess.run([str(exe)], capture_output=True).returncode, 0)

if __name__ == '__main__':
    unittest.main()
