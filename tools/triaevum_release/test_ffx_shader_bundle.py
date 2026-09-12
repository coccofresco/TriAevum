import json
import tempfile
import unittest
from pathlib import Path

from tools.triaevum_release.ffx_shader_bundle import export, install


class FfxShaderBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'sdk'
        for directory in ('include/FidelityFX/gpu', 'src/backends/vk/shaders/sssr',
                          'src/backends/vk/shaders/denoiser'):
            path = self.sdk / directory / 'input.h'
            path.parent.mkdir(parents=True)
            path.write_bytes(b'input\r\n')
        self.generated = self.root / 'generated'
        self.overlay = self.generated / 'source/sssr/ffx_sssr_callbacks_glsl.h'
        self.overlay.parent.mkdir(parents=True)
        self.overlay.write_bytes(b'overlay\r\n')
        (self.generated / 'ffx_test_permutations.h').write_bytes(b'permutations')
        (self.generated / 'ffx_test_blob.h').write_bytes(b'blob')
        self.bundle = self.root / 'bundle'
        self.output = self.root / 'output'
        export(self.sdk, self.generated, self.bundle)

    def test_roundtrip_and_noop_preserve_mtime_across_source_line_endings(self):
        for path in self.sdk.rglob('*.h'):
            path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n'))
        install(self.sdk, self.bundle, self.overlay, self.output)
        times = {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()}
        install(self.sdk, self.bundle, self.overlay, self.output)
        self.assertEqual(times, {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()})

    def test_source_and_overlay_changes_are_rejected(self):
        self.overlay.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'inputs/overlay'):
            install(self.sdk, self.bundle, self.overlay, self.output)
        self.assertFalse(self.output.exists())

    def test_all_headers_checked_before_any_copy(self):
        (self.bundle / 'ffx_test_blob.h').write_bytes(b'damaged')
        with self.assertRaisesRegex(ValueError, 'integrity'):
            install(self.sdk, self.bundle, self.overlay, self.output)
        self.assertFalse(self.output.exists())

    def test_escape_rejected(self):
        path = self.bundle / 'manifest.json'
        manifest = json.loads(path.read_text())
        manifest['headers']['../escape.h'] = '0' * 64
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'header path'):
            install(self.sdk, self.bundle, self.overlay, self.output)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
