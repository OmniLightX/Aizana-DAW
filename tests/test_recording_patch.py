"""Exercise configure-time patching on isolated copies, never the checkout."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
PIN = '0a5f4e6a5f53d09c89b414a44386a12df7fa1ec6'
FILES = [f'modules/tracktion_engine/playback/{name}' for name in (
    'tracktion_EditPlaybackContext.cpp', 'tracktion_EditPlaybackContext.h',
    'tracktion_TransportControl.cpp', 'devices/tracktion_WaveInputDevice.cpp')]


class RecordingPatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='aizana-patch-')
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.engine = self.root / 'extern/tracktion_engine'
        for folder in ('cmake', 'patches', 'extern/tracktion_engine'):
            (self.root / folder).mkdir(parents=True, exist_ok=True)
        for path in ('cmake/ApplyTracktionRecordingPatch.cmake',
                     'patches/tracktion-recording-errors.patch'):
            shutil.copyfile(REPO / path, self.root / path)
        for path in FILES:
            target = self.engine / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(subprocess.check_output(
                ['git', 'show', f'{PIN}:{path}'], cwd=REPO / 'extern/tracktion_engine'))

    def apply(self):
        return subprocess.run(['cmake', '-P', str(self.root / 'cmake/ApplyTracktionRecordingPatch.cmake')],
                              capture_output=True, text=True)

    def contents(self):
        return [(self.engine / path).read_bytes() for path in FILES]

    def test_apply_and_repeat(self):
        original = self.contents()
        result = self.apply()
        self.assertEqual(result.returncode, 0, result.stderr)
        patched = self.contents()
        self.assertNotEqual(original, patched)
        result = self.apply()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.contents(), patched)

    def test_local_edits_are_preserved(self):
        path = self.engine / FILES[0]
        path.write_bytes(path.read_bytes() + b'\n// user edit\n')
        before = self.contents()
        self.assertNotEqual(self.apply().returncode, 0)
        self.assertEqual(self.contents(), before)

    def test_partial_patch_is_rejected_without_writes(self):
        original = self.contents()
        self.assertEqual(self.apply().returncode, 0)
        (self.engine / FILES[0]).write_bytes(original[0])
        before = self.contents()
        self.assertNotEqual(self.apply().returncode, 0)
        self.assertEqual(self.contents(), before)

    def test_missing_dependency_is_actionable(self):
        (self.engine / FILES[0]).unlink()
        result = self.apply()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('git submodule update --init', result.stderr)


if __name__ == '__main__':
    unittest.main()
