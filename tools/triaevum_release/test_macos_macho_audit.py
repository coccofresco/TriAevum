"""Synthetic headers exercise rejection gates without executing native code."""

import struct
import os
import plistlib
import shutil
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

from macos_macho_audit import inspect_macho, require_macho, _binary_errors, version, launcher_identity


def path_command(command, text):
    start = 12 if command == 0x8000001C else 24
    payload = text.encode() + b"\0"
    length = (start + len(payload) + 7) & ~7
    return struct.pack("<III", command, length, start) + bytes(start - 12) + payload + bytes(length - start - len(payload))


def macho(path, *, minimum=0x000E0000, cpu=0x0100000C, kind=2, platform=1, paths=(), signed=True):
    commands = [struct.pack("<6I", 0x32, 24, platform, minimum, 0x000E0000, 0)]
    commands += [path_command(command, text) for command, text in paths]
    if signed:
        end = 32 + sum(map(len, commands)) + 16
        commands.append(struct.pack("<4I", 0x1D, 16, end, 16))
    data = b"".join(commands)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack("<8I", 0xFEEDFACF, cpu, 0, kind, len(commands), len(data), 0, 0)
                     + data + (bytes(16) if signed else b""))


class MachOAuditTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.app = Path(temporary.name) / "TriAevum.app"
        self.binary = self.app / "Contents/MacOS/TriAevum"

    def test_accepts_thin_arm64_and_numeric_version_ceiling(self):
        macho(self.binary)
        self.assertEqual(require_macho(self.binary, minimum_macos="14.0", shared=False)["minimum_macos"], "14.0.0")
        self.assertGreater(version("14.10"), version("14.9.9"))
        macho(self.binary, minimum=0x000F0000)
        with self.assertRaisesRegex(ValueError, "above"):
            require_macho(self.binary, minimum_macos="14.0")

    def test_launcher_digest_changes_for_code_but_not_resource_resigning(self):
        macho(self.binary)
        before = launcher_identity(self.binary)
        data = bytearray(self.binary.read_bytes())
        data[-16:] = b"signaturefixture"
        self.binary.write_bytes(data)
        self.assertEqual(launcher_identity(self.binary), before)
        data += bytes(16)
        struct.pack_into("<I", data, 68, 32)
        self.binary.write_bytes(data)
        self.assertEqual(launcher_identity(self.binary), before)
        data[24] ^= 1  # Mach-O flags, part of the signed executable identity.
        self.binary.write_bytes(data)
        self.assertNotEqual(launcher_identity(self.binary)["sha256"], before["sha256"])

    def test_rejects_wrong_cpu_platform_filetype_and_unsigned(self):
        for options in ({"cpu": 0x01000007}, {"platform": 2}, {"kind": 1}):
            macho(self.binary, **options)
            with self.assertRaises(ValueError):
                inspect_macho(self.binary)
        macho(self.binary, kind=6)
        with self.assertRaisesRegex(ValueError, "file type"):
            require_macho(self.binary, minimum_macos="14.0", shared=False)
        macho(self.binary, signed=False)
        self.assertTrue(any("no code signature" in error for error in _binary_errors(self.binary, self.app, "14.0")))

    def test_rejects_malformed_command_and_signature_bounds(self):
        for offset, value in ((20, 0xFFFFFFFF), (16, 100), (36, 0), (40, 0), (64, 0xFFFFFFFF)):
            macho(self.binary)
            data = bytearray(self.binary.read_bytes())
            struct.pack_into("<I", data, offset, value)
            self.binary.write_bytes(data)
            with self.assertRaises(ValueError, msg=str(offset)):
                inspect_macho(self.binary)
        for magic in (b"MZ\0\0", b"\x7fELF", b"\xca\xfe\xba\xbe"):
            self.binary.write_bytes(magic + bytes(60))
            with self.assertRaises(ValueError):
                inspect_macho(self.binary)

    def test_bundle_dependencies_are_present_and_do_not_reach_build_machine(self):
        dependency = self.app / "Contents/Frameworks/libSDL2-2.0.0.dylib"
        macho(dependency, kind=6)
        paths = [(0xC, "@rpath/libSDL2-2.0.0.dylib"), (0xC, "/usr/lib/libSystem.B.dylib"),
                 (0x8000001C, "@executable_path/../Frameworks")]
        macho(self.binary, paths=paths)
        self.assertEqual(list(_binary_errors(self.binary, self.app, "14.0")), [])
        for invalid_paths in (paths[:2], paths + [paths[-1]]):
            macho(self.binary, paths=invalid_paths)
            self.assertTrue(any("exactly one LC_RPATH" in error
                                for error in _binary_errors(self.binary, self.app, "14.0")))
        macho(self.binary, paths=[(0xC, "/usr/lib/libSystem.B.dylib")])
        self.assertEqual(list(_binary_errors(self.binary, self.app, "14.0")), [])
        macho(self.binary, paths=paths)
        dependency.unlink()
        self.assertTrue(any("Missing bundle dependency" in error for error in _binary_errors(self.binary, self.app, "14.0")))
        for command, path in ((0xC, "/opt/local/libSDL2.dylib"),
                              (0x8000001C, "/tmp/build/lib"),
                              (0x8000001C, "@loader_path/../../../../outside")):
            macho(self.binary, paths=[(command, path)])
            self.assertTrue(list(_binary_errors(self.binary, self.app, "14.0")))

    @unittest.skipUnless(sys.platform == "darwin" and os.environ.get("TRIAEVUM_MACOS_LAUNCHER"),
                         "requires a publisher-built macOS launcher")
    def test_real_bundle_signing_preserves_launcher_code_digest(self):
        launcher = self.app / "Contents/MacOS/TriAevumLauncher"
        launcher.parent.mkdir(parents=True)
        shutil.copyfile(os.environ["TRIAEVUM_MACOS_LAUNCHER"], launcher)
        launcher.chmod(0o755)
        (self.app / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": launcher.name, "CFBundleIdentifier": "org.triaevum.signing-fixture",
            "CFBundlePackageType": "APPL", "CFBundleVersion": "1"}))
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(launcher)], check=True,
                       capture_output=True)
        before = launcher_identity(launcher)
        helper = self.app / "Contents/Resources/forge/triaevum-forge"
        helper.parent.mkdir(parents=True)
        shutil.copyfile(launcher, helper)
        helper.chmod(0o755)
        helper_bytes = helper.read_bytes()
        for name in ("recipes", "resources", "source", "docs", "titles/synthetic"):
            directory = self.app / "Contents/Resources" / name
            directory.mkdir(parents=True, exist_ok=True)
            (directory / "fixture.json").write_text("{}\n")
        manifest = self.app / "Contents/Resources/fixture.json"
        manifest.parent.mkdir(exist_ok=True)
        manifest.write_text("{}\n")
        for payload in ("{}\n", '{"changed":true}\n'):
            manifest.write_text(payload)
            subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(self.app)], check=True)
            subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(self.app)], check=True,
                           capture_output=True)
            self.assertEqual(launcher_identity(launcher), before)
            self.assertEqual(helper.read_bytes(), helper_bytes)


if __name__ == "__main__":
    unittest.main()
