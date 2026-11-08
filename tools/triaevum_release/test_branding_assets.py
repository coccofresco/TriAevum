"""The branding outputs (branding/README.md) exist at their sizes and every
consumer names them: the executables' icon resources, the Forge's compiled-in
images, the Linux desktop entries and packagers, the Android launcher icon."""

import json
import re
import struct
import unittest
from pathlib import Path

RELEASE = Path(__file__).resolve().parent
ROOT = RELEASE.parents[1]
BRANDING = RELEASE / "branding"
APP_ID = "io.github.coccofresco.TriAevum"
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_size(data: bytes) -> tuple[int, int]:
    assert data.startswith(PNG_SIGNATURE) and data[12:16] == b"IHDR"
    return struct.unpack(">II", data[16:24])


def ico_frames(path: Path) -> list[tuple[int, bytes]]:
    data = path.read_bytes()
    reserved, kind, count = struct.unpack_from("<HHH", data)
    assert (reserved, kind) == (0, 1)
    frames = []
    for index in range(count):
        width, _height, _colors, _reserved, _planes, bits, size, offset = struct.unpack_from(
            "<BBBBHHII", data, 6 + 16 * index)
        assert bits == 32
        frames.append((width or 256, data[offset:offset + size]))
    return frames


class BrandingAssetTests(unittest.TestCase):
    def test_windows_icons_hold_every_size(self):
        for name in ("triaevum.ico", "forge.ico"):
            with self.subTest(name=name):
                frames = ico_frames(BRANDING / "windows" / name)
                self.assertEqual([size for size, _ in frames], ICO_SIZES)
                for size, frame in frames:
                    if size == 256:
                        self.assertEqual(png_size(frame), (256, 256))
                    else:
                        header, width, height = struct.unpack_from("<Iii", frame)
                        self.assertEqual((header, width, height), (40, size, size * 2))

    def test_executables_embed_their_icons(self):
        rc = (ROOT / "tools/oot3d/native_game_runtime/triaevum_windows.rc").read_text(encoding="utf-8")
        target = re.search(r'^1 ICON "([^"]+)"$', rc, re.MULTILINE)
        self.assertIsNotNone(target)
        self.assertTrue((ROOT / "tools/oot3d/native_game_runtime" / target[1]).resolve().samefile(
            BRANDING / "windows/triaevum.ico"))
        self.assertIn("native_game_runtime/triaevum_windows.rc", (ROOT / "CMake/oot3d/game.cmake").read_text())
        for crate in ("forge-desktop", "forge-update-helper"):
            folder = ROOT / "tools/triaevum_forge/crates" / crate
            # The default features' icon: the last (else) call; title-mm3d's is checked below.
            ico = re.findall(r'embed_windows_icon\("([^"]+)"\)', (folder / "build.rs").read_text(encoding="utf-8"))
            self.assertTrue((folder / ico[-1]).resolve().samefile(BRANDING / "windows/forge.ico"), crate)

    def test_forge_compiles_in_its_window_images(self):
        theme = ROOT / "tools/triaevum_forge/crates/forge-desktop/src/theme.rs"
        text = theme.read_text(encoding="utf-8")
        # The default (OoT3D) Forge's two images; the title-mm3d feature's are
        # checked in MM3DBrandingTests, its test's OoT3D copies are not counted.
        default = re.findall(r'#\[cfg\(not\(feature = "title-mm3d"\)\)\]\nconst \w+: &\[u8\] = include_bytes!\("([^"]+)"\)', text)
        self.assertEqual(len(default), 2)
        included = re.findall(r'include_bytes!\("([^"]+)"\)', text)
        for relative in included:
            self.assertTrue((theme.parent / relative).is_file(), relative)
        self.assertEqual(png_size((BRANDING / "forge/forge_window_icon.png").read_bytes()), (256, 256))
        background = (BRANDING / "masters/forge_background.jpg").read_bytes()
        self.assertTrue(background.startswith(b"\xff\xd8"))
        self.assertLess(len(background), 500_000)

    def test_game_window_icon_is_64_pixels_rgba(self):
        text = (ROOT / "tools/oot3d/native_demo_host/triaevum_window_icon_rgba.inc").read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"0x[0-9a-f]{2}", text)), 64 * 64 * 4)

    def test_linux_icons_and_desktop_entries(self):
        hicolor = BRANDING / "linux/hicolor"
        for size in (64, 128, 256, 512):
            for name in (APP_ID, f"{APP_ID}.Forge"):
                path = hicolor / f"{size}x{size}/apps/{name}.png"
                self.assertEqual(png_size(path.read_bytes()), (size, size), path)
        for folder in ("appimage", "flatpak"):
            entry = (RELEASE / folder / f"{APP_ID}.desktop").read_text(encoding="utf-8")
            with self.subTest(folder=folder):
                main, _, actions = entry.partition("\n[Desktop Action")
                self.assertIn(f"\nIcon={APP_ID}\n", main)
                forge = actions[actions.index(" Forge]"):]
                self.assertIn(f"\nIcon={APP_ID}.Forge\n", forge)
        manifest = json.loads((RELEASE / f"flatpak/{APP_ID}.json").read_text(encoding="utf-8"))
        module = manifest["modules"][0]
        self.assertIn("cp -r icons/hicolor /app/share/icons/", module["build-commands"])
        self.assertIn({"type": "dir", "path": "icons", "dest": "icons"}, module["sources"])

    def test_android_launcher_icon(self):
        res = ROOT / "ports/android/app/src/main/res"
        for density, legacy, layer in (("mdpi", 48, 108), ("hdpi", 72, 162), ("xhdpi", 96, 216),
                                       ("xxhdpi", 144, 324), ("xxxhdpi", 192, 432)):
            folder = res / f"mipmap-{density}"
            for name, size in (("ic_launcher.png", legacy), ("ic_launcher_round.png", legacy),
                               ("ic_launcher_foreground.png", layer), ("ic_launcher_background.png", layer)):
                self.assertEqual(png_size((folder / name).read_bytes()), (size, size), folder / name)
        for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
            adaptive = (res / "mipmap-anydpi-v26" / name).read_text(encoding="utf-8")
            self.assertIn('@mipmap/ic_launcher_foreground', adaptive)
            self.assertIn('@mipmap/ic_launcher_background', adaptive)
        manifest = (ROOT / "ports/android/app/src/main/AndroidManifest.xml").read_text(encoding="utf-8")
        self.assertIn('android:icon="@mipmap/ic_launcher"', manifest)
        self.assertIn('android:roundIcon="@mipmap/ic_launcher_round"', manifest)

    def test_artwork_provenance_ships(self):
        layout = json.loads((RELEASE / "runtime_release_layout.example.json").read_text(encoding="utf-8"))
        self.assertIn({"source": "LICENSES/TriAevum-artwork.md", "path": "LICENSES/TriAevum-artwork.md",
                       "role": "license"}, layout["files"])
        self.assertIn("supplied by\nthe project owner", (ROOT / "LICENSES/TriAevum-artwork.md").read_text())


MM3D_BRANDING = ROOT / "titles/mm3d/branding"


@unittest.skipUnless(MM3D_BRANDING.is_dir(), "titles/mm3d is local (never published)")
class MM3DBrandingTests(unittest.TestCase):
    """MM3D's own branding (titles/mm3d/branding/README.md): generated by
    generate_branding.py --title mm3d, consumed under the per-title switches."""

    def test_outputs_exist_at_their_sizes(self):
        for name in ("triaevum.ico", "forge.ico"):
            frames = ico_frames(MM3D_BRANDING / "windows" / name)
            self.assertEqual([size for size, _ in frames], ICO_SIZES, name)
            self.assertEqual(png_size(frames[-1][1]), (256, 256), name)
        self.assertEqual(png_size((MM3D_BRANDING / "forge/forge_window_icon.png").read_bytes()), (256, 256))
        for name in ("game_icon.png", "forge_icon.png"):
            self.assertEqual(png_size((MM3D_BRANDING / "masters" / name).read_bytes()), (1024, 1024), name)
        background = (MM3D_BRANDING / "masters/forge_background.jpg").read_bytes()
        self.assertTrue(background.startswith(b"\xff\xd8"))
        self.assertLess(len(background), 500_000)
        text = (MM3D_BRANDING / "mm3d_window_icon_rgba.inc").read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"0x[0-9a-f]{2}", text)), 64 * 64 * 4)
        # Owner-deferred or without a product path: no Android or Linux outputs.
        self.assertFalse((MM3D_BRANDING / "linux").exists())
        self.assertFalse((MM3D_BRANDING / "android").exists())

    def test_outputs_differ_from_oot3d(self):
        for relative in ("windows/triaevum.ico", "windows/forge.ico", "forge/forge_window_icon.png",
                         "masters/forge_background.jpg"):
            self.assertNotEqual((MM3D_BRANDING / relative).read_bytes(), (BRANDING / relative).read_bytes(), relative)

    def test_forge_consumes_them_under_the_feature(self):
        crate = ROOT / "tools/triaevum_forge/crates/forge-desktop"
        self.assertRegex((crate / "Cargo.toml").read_text(encoding="utf-8"), r"(?m)^title-mm3d = ")
        build = (crate / "build.rs").read_text(encoding="utf-8")
        self.assertIn("CARGO_FEATURE_TITLE_MM3D", build)
        icos = re.findall(r'embed_windows_icon\("([^"]+)"\)', build)
        self.assertEqual(len(icos), 2)
        self.assertTrue((crate / icos[0]).resolve().samefile(MM3D_BRANDING / "windows/forge.ico"))
        self.assertTrue((crate / icos[1]).resolve().samefile(BRANDING / "windows/forge.ico"))
        theme = crate / "src/theme.rs"
        mm3d = re.findall(r'#\[cfg\(feature = "title-mm3d"\)\]\nconst (\w+): &\[u8\] = include_bytes!\("([^"]+)"\)',
                          theme.read_text(encoding="utf-8"))
        self.assertEqual({name for name, _ in mm3d}, {"BACKGROUND_JPEG", "WINDOW_ICON_PNG"})
        expected = {"BACKGROUND_JPEG": "masters/forge_background.jpg", "WINDOW_ICON_PNG": "forge/forge_window_icon.png"}
        for name, relative in mm3d:
            self.assertTrue((theme.parent / relative).resolve().samefile(MM3D_BRANDING / expected[name]), name)

    def test_game_executable_and_window_consume_them(self):
        rc = (MM3D_BRANDING / "mm3d_windows.rc").read_text(encoding="utf-8")
        target = re.search(r'^1 ICON "([^"]+)"$', rc, re.MULTILINE)
        self.assertTrue((MM3D_BRANDING / target[1]).resolve().samefile(MM3D_BRANDING / "windows/triaevum.ico"))
        game = (ROOT / "CMake/oot3d/game.cmake").read_text(encoding="utf-8")
        self.assertIn('set(TRIAEVUM_PRODUCT_TITLE "oot3d" CACHE STRING', game)
        self.assertIn("set(_triaevum_product_rc titles/mm3d/branding/mm3d_windows.rc)", game)
        self.assertIn("titles/mm3d/adapter/mm3d_window_icon.cpp", game)
        icon = (ROOT / "titles/mm3d/adapter/mm3d_window_icon.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "../branding/mm3d_window_icon_rgba.inc"', icon)
        session = (ROOT / "titles/mm3d/adapter/mm3d_title_session.cpp").read_text(encoding="utf-8")
        self.assertIn("WindowIcon() const override { return &Mm3dWindowIcon(); }", session)


if __name__ == "__main__":
    unittest.main()
