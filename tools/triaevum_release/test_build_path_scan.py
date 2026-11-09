import tempfile
import unittest
from pathlib import Path

import build_path_scan


class BuildPathScanTests(unittest.TestCase):
    def test_absolute_source_paths_are_found(self):
        data = (b"\0[X:/checkout/runtime/three_ds_recomp/src/ship/debug/CrashHandler.cpp:73]\0"
                b"/home/builder/src/nri/Source/VK/DeviceVK.cpp\0D:\\deps\\imgui\\imgui.h\0")
        self.assertEqual(len(build_path_scan.find_host_paths(data)), 3)

    def test_relative_and_data_paths_are_clean(self):
        data = (b"[runtime/three_ds_recomp/src/ship/debug/CrashHandler.cpp:73]\0"
                b"m:/menu/01_US_ENGLISH/title.bclim\0https://example.org/a.html\0"
                b"deps/nri/Source/VK/DeviceVK.cpp\0")
        self.assertEqual(build_path_scan.find_host_paths(data), [])

    def test_exit_codes(self):
        with tempfile.TemporaryDirectory() as folder:
            clean, dirty = Path(folder, "clean.bin"), Path(folder, "dirty.bin")
            clean.write_bytes(b"runtime/src/a.cpp")
            dirty.write_bytes(b"Q:/work/runtime/src/a.cpp")
            self.assertEqual(build_path_scan.main([str(clean)]), 0)
            self.assertEqual(build_path_scan.main([str(dirty)]), 1)
            self.assertEqual(build_path_scan.main([str(Path(folder, "none"))]), 2)

    def test_allow_markers_cover_third_party_only(self):
        with tempfile.TemporaryDirectory() as folder:
            vendor = Path(folder, "vendor.bin")
            vendor.write_bytes(b"Q:\\vcpkg\\buildtrees\\sdl2\\SDL_render_sw.c\0"
                               b"Q:/dvs/p4/build/sw/ngx/common_lib.cpp\0")
            mixed = Path(folder, "mixed.bin")
            mixed.write_bytes(vendor.read_bytes() + b"Q:/work/runtime/src/a.cpp")
            allow = ["--allow", "/vcpkg/buildtrees/", "--allow", "/dvs/p4/build/"]
            self.assertEqual(build_path_scan.main([str(vendor)]), 1)
            self.assertEqual(build_path_scan.main([str(vendor), *allow]), 0)
            self.assertEqual(build_path_scan.main([str(mixed), *allow]), 1)


if __name__ == "__main__":
    unittest.main()
