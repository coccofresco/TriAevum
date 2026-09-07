"""Exercise the actual dependency patch without compiling the renderer."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class CmakePatchTests(unittest.TestCase):
    def test_fidelityfx_patch_changes_content_once_and_preserves_timestamp(self):
        cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake is not available")
        root = Path(__file__).resolve().parents[2]
        patch = root / "runtime/three_ds_recomp/cmake/patches/oot3d_fidelityfx_vk_core_aliases.cmake"
        if not patch.is_file():
            self.skipTest("Renderer submodule is not checked out; dependency patch test is unavailable")
        source = '''        backendContext->vkFunctionTable.vkGetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)vkDeviceContext->vkDeviceProcAddr(backendContext->device, "vkGetBufferMemoryRequirements2KHR");
            deviceCapabilities->fp16Supported = (bool)shaderFloat18Int8Features.shaderFloat16;
        descriptorPoolCreateInfo.poolSizeCount = 5;
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            backend = directory / "sdk/src/backends/vk/ffx_vk.cpp"
            backend.parent.mkdir(parents=True)
            backend.write_text(source, encoding="utf-8")
            command = [cmake, "-DFFX_SOURCE_DIR=" + str(directory), "-P", str(patch)]
            subprocess.run(command, check=True, capture_output=True, timeout=15)
            patched = backend.read_bytes()
            self.assertIn(b'"vkGetBufferMemoryRequirements2"', patched)
            self.assertIn(b"fp16Supported = false", patched)
            self.assertIn(b"poolSizeCount = 6", patched)
            # A fixed old timestamp makes an unnecessary write observable without sleeps.
            os.utime(backend, (1_600_000_000, 1_600_000_000))
            timestamp = backend.stat().st_mtime_ns
            subprocess.run(command, check=True, capture_output=True, timeout=15)
            self.assertEqual(backend.read_bytes(), patched)
            self.assertEqual(backend.stat().st_mtime_ns, timestamp)


if __name__ == "__main__":
    unittest.main()
