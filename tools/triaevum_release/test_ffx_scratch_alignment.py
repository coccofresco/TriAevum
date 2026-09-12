import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


PATCH = Path(__file__).resolve().parents[2] / (
    'runtime/three_ds_recomp/cmake/patches/'
    'oot3d_fidelityfx_vk_scratch_alignment.cmake')


@unittest.skipUnless(shutil.which('cmake'), 'CMake is required')
class FfxScratchAlignmentTests(unittest.TestCase):
    def test_checked_patch_is_idempotent_and_rejects_changed_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            backend = root / 'sdk/src/backends/vk/ffx_vk.cpp'
            backend.parent.mkdir(parents=True)
            backend.write_text(
                'pipelineArraySize + resourceArraySize + contextArraySize,\n'
                '        // Map context array\n'
                '        backendContext->pEffectContexts = '
                '(BackendContext_VK::EffectContext*)pMem;\n')
            command = ['cmake', f'-DFFX_SOURCE_DIR={root.as_posix()}', '-P', str(PATCH)]
            subprocess.run(command, check=True, capture_output=True)
            fixed = backend.read_bytes()
            timestamp = backend.stat().st_mtime_ns
            self.assertIn(b'contextArraySize + alignof(', fixed)
            self.assertIn(b'reinterpret_cast<uintptr_t>(pMem)', fixed)
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(fixed, backend.read_bytes())
            self.assertEqual(timestamp, backend.stat().st_mtime_ns)
            backend.write_text('different donor layout\n')
            result = subprocess.run(command, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(backend.read_text(), 'different donor layout\n')

    def test_padding_covers_every_absolute_pointer_alignment(self):
        alignment = 32
        for address in range(4096, 8192):
            aligned = (address + alignment - 1) & ~(alignment - 1)
            self.assertEqual(aligned % alignment, 0)
            self.assertGreaterEqual(aligned, address)
            self.assertLessEqual(aligned - address, alignment - 1)


if __name__ == '__main__':
    unittest.main()
