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


import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dlss_runtime_signature as sig  # noqa: E402


def _pad(data):
    return data + b'\0' * (-len(data) % 4)


def _block(key, value=b'', value_length=None, value_type=0, children=b''):
    if value_length is None:
        value_length = len(value) // 2 if value_type == 1 else len(value)
    head = _pad(b'\0' * 6 + key.encode('utf-16-le') + b'\0\0')
    body = head + _pad(value) + children
    return _pad(struct.pack('<HHH', len(body), value_length, value_type) + body[6:])


def _string(key, text):
    return _block(key, (text + '\0').encode('utf-16-le'), value_type=1)


def synthetic_pe(product_version=None, fixed=(310, 7, 0, 0)):
    """A minimal PE32+ image with a .rsrc section holding one VS_VERSIONINFO."""
    fixed_info = struct.pack('<13I', 0xFEEF04BD, 0x10000, 0, 0,
                             (fixed[0] << 16) | fixed[1], (fixed[2] << 16) | fixed[3],
                             0, 0, 0, 0, 0, 0, 0)
    children = b''
    if product_version is not None:
        table = _block('040904b0', children=_string('CompanyName', 'NVIDIA Corporation')
                       + _string('ProductVersion', product_version), value_type=1)
        children = _block('StringFileInfo', children=table, value_type=1)
    version = _block('VS_VERSION_INFO', fixed_info, value_length=len(fixed_info), children=children)
    section_rva, raw_offset = 0x1000, 0x200

    def directory(entries):
        return struct.pack('<IIHHHH', 0, 0, 0, 0, 0, len(entries)) + b''.join(
            struct.pack('<II', name, target) for name, target in entries)
    root = directory([(16, 0x80000000 | 24)])
    type_dir = directory([(1, 0x80000000 | 48)])
    name_dir = directory([(0x409, 72)])
    data_entry = struct.pack('<IIII', section_rva + 88, len(version), 0, 0)
    rsrc = root + type_dir + name_dir + data_entry + version
    rsrc += b'\0' * (-len(rsrc) % 0x200)
    optional = bytearray(240)
    struct.pack_into('<H', optional, 0, 0x20B)
    struct.pack_into('<I', optional, 108, 16)
    struct.pack_into('<II', optional, 112 + 2 * 8, section_rva, len(rsrc))
    coff = struct.pack('<HHIIIHH', 0x8664, 1, 0, 0, 0, len(optional), 0x2022)
    section = b'.rsrc\0\0\0' + struct.pack('<IIIIIIHHI', len(rsrc), section_rva, len(rsrc), raw_offset,
                                          0, 0, 0, 0, 0x40000040)
    dos = bytearray(64)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3C, 64)
    headers = bytes(dos) + b'PE\0\0' + coff + bytes(optional) + section
    return headers + b'\0' * (raw_offset - len(headers)) + rsrc


SAMPLE_OK = """Signer's certificate:
\t------------------
\tSigner #0:
\t\tSubject: CN=NVIDIA Corporation,OU=1005bk6,O=NVIDIA Corporation,L=Santa Clara,ST=California,C=US
\t\tIssuer : CN=DigiCert Trusted G4 Code Signing RSA4096 SHA384 2021 CA1,O=DigiCert\\, Inc.,C=US
CRL online verification disabled
Signature verification: ok

Number of verified signatures: 1
Succeeded
"""


class HostIndependentDlssCheckTests(unittest.TestCase):
    def test_product_version_string_table(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'nvngx_dlss.dll'
            path.write_bytes(synthetic_pe('310.7.0.0', fixed=(1, 2, 3, 4)))
            self.assertEqual(sig.product_version_string(path), '310.7.0.0')

    def test_product_version_falls_back_to_fixed_info(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'nvngx_dlss.dll'
            path.write_bytes(synthetic_pe(None, fixed=(310, 7, 0, 9)))
            self.assertEqual(sig.product_version_string(path), '310.7.0.9')

    def test_not_a_pe_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'unsigned.dll'
            path.write_bytes(b'not a signed NVIDIA runtime')
            with self.assertRaises(sig.ValidationError):
                sig.product_version_string(path)

    def test_version_comparison_is_major_minor_build(self):
        self.assertTrue(sig.versions_match('310.7.0.0', '310.7.0'))
        self.assertTrue(sig.versions_match('310,7,0,5', '310.7.0'))
        self.assertFalse(sig.versions_match('310.7.1.0', '310.7.0'))
        self.assertFalse(sig.versions_match('310.7.0', '0.0.0'))

    def test_osslsigncode_output_accepts_nvidia_leaf(self):
        self.assertIn('O=NVIDIA Corporation', sig.check_osslsigncode_output(0, SAMPLE_OK))

    def test_osslsigncode_output_rejects_failure_and_other_signer(self):
        with self.assertRaisesRegex(sig.ValidationError, 'Authenticode verification failed'):
            sig.check_osslsigncode_output(1, SAMPLE_OK.replace('verification: ok', 'verification: failed'))
        with self.assertRaisesRegex(sig.ValidationError, 'Authenticode verification failed'):
            sig.check_osslsigncode_output(0, SAMPLE_OK.replace('Succeeded', 'Failed'))
        other = SAMPLE_OK.replace('O=NVIDIA Corporation,L', 'O=Not NVIDIA Corporation Ltd,L')
        with self.assertRaisesRegex(sig.ValidationError, 'not NVIDIA'):
            sig.check_osslsigncode_output(0, other)

    def test_trust_bundle_pin_is_enforced(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / 'nvngx_dlss.dll'
            runtime.write_bytes(synthetic_pe('310.7.0.0'))
            bundle = root / 'bundle.pem'
            bundle.write_bytes(b'bundle')
            with self.assertRaisesRegex(sig.ValidationError, 'pinned'):
                sig.validate(runtime, '310.7.0', root / 'missing-osslsigncode', bundle, '00' * 32)


@unittest.skipIf(os.name == 'nt', 'the osslsigncode route is for non-Windows build hosts')
class OsslsigncodeRuntimeTests(unittest.TestCase):
    """Real check: TRIAEVUM_TEST_OSSLSIGNCODE, TRIAEVUM_TEST_DLSS_TRUST_BUNDLE(_SHA256),
    TRIAEVUM_TEST_DLSS_RUNTIME and TRIAEVUM_TEST_DLSS_VERSION."""

    NAMES = ('TRIAEVUM_TEST_OSSLSIGNCODE', 'TRIAEVUM_TEST_DLSS_TRUST_BUNDLE',
             'TRIAEVUM_TEST_DLSS_TRUST_BUNDLE_SHA256', 'TRIAEVUM_TEST_DLSS_RUNTIME',
             'TRIAEVUM_TEST_DLSS_VERSION')

    def setUp(self):
        self.env = {name: os.environ.get(name) for name in self.NAMES}
        if not all(self.env.values()):
            self.skipTest('osslsigncode, pinned bundle and a signed NVIDIA runtime required')
        self.cmake = os.environ.get('TRIAEVUM_TEST_CMAKE') or shutil.which('cmake')
        if not self.cmake:
            self.skipTest('CMake required')
        self.script = (Path(__file__).resolve().parents[2]
                       / 'runtime/three_ds_recomp/cmake/ValidateNvidiaDlssRuntime.cmake')

    def cmake_validate(self, runtime, version, tools=True):
        args = [self.cmake, f'-DRUNTIME_PATH={runtime}', f'-DEXPECTED_VERSION={version}']
        if tools:
            args += [f'-DOSSLSIGNCODE={self.env["TRIAEVUM_TEST_OSSLSIGNCODE"]}',
                     f'-DTRUST_BUNDLE={self.env["TRIAEVUM_TEST_DLSS_TRUST_BUNDLE"]}',
                     f'-DTRUST_BUNDLE_SHA256={self.env["TRIAEVUM_TEST_DLSS_TRUST_BUNDLE_SHA256"]}']
        return subprocess.run(args + ['-P', str(self.script)],
                              capture_output=True, text=True, errors='replace', timeout=180)

    def test_real_runtime_wrong_version_and_tampered_image(self):
        runtime = self.env['TRIAEVUM_TEST_DLSS_RUNTIME']
        version = self.env['TRIAEVUM_TEST_DLSS_VERSION']
        ok = self.cmake_validate(runtime, version)
        self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
        self.assertIn('NVIDIA DLSS runtime verified', ok.stdout + ok.stderr)
        wrong = self.cmake_validate(runtime, '0.0.0')
        self.assertNotEqual(wrong.returncode, 0)
        self.assertIn('does not match required version', wrong.stdout + wrong.stderr)
        with tempfile.TemporaryDirectory() as directory:
            tampered = Path(directory) / 'nvngx_dlss.dll'
            data = bytearray(Path(runtime).read_bytes())
            data[0x400] ^= 1
            tampered.write_bytes(bytes(data))
            bad = self.cmake_validate(tampered, version)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('Authenticode verification failed', ' '.join((bad.stdout + bad.stderr).split()))

    def test_windows_runtime_without_tools_is_refused_not_skipped(self):
        result = self.cmake_validate(self.env['TRIAEVUM_TEST_DLSS_RUNTIME'], '310.7.0', tools=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('-DOSSLSIGNCODE', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
