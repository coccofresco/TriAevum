import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'nt', 'Authenticode requires Windows')
class DlssRuntimeValidationTests(unittest.TestCase):
    def validate(self, runtime, version, module_path):
        cmake = os.environ.get('TRIAEVUM_TEST_CMAKE') or shutil.which('cmake')
        if not cmake:
            self.skipTest('CMake required')
        script = Path(__file__).resolve().parents[2] / 'runtime/three_ds_recomp/cmake/ValidateNvidiaDlssRuntime.cmake'
        return subprocess.run(
            [cmake, f'-DRUNTIME_PATH={runtime}', f'-DEXPECTED_VERSION={version}', '-P', str(script)],
            env=dict(os.environ, PSModulePath=module_path),
            capture_output=True, text=True, errors='replace', timeout=45)

    def test_unsigned_file_is_rejected_with_foreign_module_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / 'unsigned.dll'
            runtime.write_bytes(b'not a signed NVIDIA runtime')
            result = self.validate(runtime, '310.7.0', str(root / 'foreign-modules'))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Authenticode status', result.stdout + result.stderr)
            self.assertNotIn('CouldNotAutoloadMatchingModule', result.stdout + result.stderr)

    def test_real_signed_runtime_and_wrong_version(self):
        runtime = os.environ.get('TRIAEVUM_TEST_DLSS_RUNTIME')
        version = os.environ.get('TRIAEVUM_TEST_DLSS_VERSION')
        if not runtime or not version:
            self.skipTest('Explicit signed NVIDIA test runtime and version required')
        with tempfile.TemporaryDirectory() as directory:
            result = self.validate(runtime, version, directory)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('NVIDIA DLSS runtime verified', result.stdout)
            rejected = self.validate(runtime, '0.0.0', directory)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('does not match required version', rejected.stdout + rejected.stderr)


if __name__ == '__main__':
    unittest.main()
