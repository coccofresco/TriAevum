"""Publisher signatures of release packages (UX plan WP-04B, owner decision 10)."""

import json
import tempfile
import unittest
import zipfile
from pathlib import Path

import release_signature
from archive_release import create_release_archive
from audit_release import audit_release
from release_platform import WINDOWS
from test_rust_forge_release import write_rust_package


def keyring(path: Path, *entries) -> Path:
    path.write_text(json.dumps({"format": release_signature.KEYS_FORMAT, "comment": "test", "keys": list(entries)}))
    return path


class ReleaseSignatureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.package = self.root / "TriAevum-test-windows"
        write_rust_package(self.package, WINDOWS.target)
        self.key = self.root / "keys" / "release.pem"
        self.entry = release_signature.generate_key(self.key, "test key")
        self.keys = keyring(self.root / "update-signing-keys.json", self.entry)

    def tearDown(self):
        self.directory.cleanup()

    def test_a_signed_package_audits_and_archives_with_its_signature(self):
        self.assertTrue(audit_release(self.package, signing_keys=self.keys).ok)
        signature = release_signature.sign_package(self.package, self.key, self.keys)
        self.assertEqual(signature, self.package / "docs/release-signature.json")
        self.assertEqual(release_signature.verify_package(self.package, self.keys), [])
        result = audit_release(self.package, signing_keys=self.keys)
        self.assertTrue(result.ok, result.errors)
        self.assertTrue(result.summary["signed"])
        archive = create_release_archive(self.package, self.root / "TriAevum-test-windows.zip", signing_keys=self.keys)
        self.assertTrue(archive["signed"])
        with zipfile.ZipFile(archive["archive"]) as package:
            self.assertIn("TriAevum-test-windows/docs/release-signature.json", package.namelist())

    def test_tampering_and_unknown_keys_fail_the_audit(self):
        release_signature.sign_package(self.package, self.key, self.keys)
        signature = self.package / "docs/release-signature.json"
        document = json.loads(signature.read_text())
        text = document["signature"]
        document["signature"] = ("1" if text[0] == "0" else "0") + text[1:]
        signature.write_text(json.dumps(document))
        self.assertIn("release signature does not verify", audit_release(self.package, signing_keys=self.keys).errors)
        stranger = keyring(self.root / "stranger.json")
        release_signature.sign_package(self.package, self.key, self.keys)
        errors = audit_release(self.package, signing_keys=stranger).errors
        self.assertTrue(any("is not in update-signing-keys.json" in error for error in errors), errors)
        manifest = self.package / "docs/release-manifest.json"
        manifest.write_bytes(manifest.read_bytes().replace(b'"version": "test"', b'"version": "evil"'))
        errors = audit_release(self.package, signing_keys=self.keys).errors
        self.assertIn("release signature belongs to another manifest", errors)

    def test_the_private_key_stays_outside_the_repository_and_the_package(self):
        with self.assertRaisesRegex(release_signature.SigningError, "outside"):
            release_signature.generate_key(release_signature.REPO_ROOT / "tools" / "never.pem")
        inside = self.package / "release.pem"
        inside.write_bytes(self.key.read_bytes())
        with self.assertRaisesRegex(release_signature.SigningError, "outside"):
            release_signature.sign_package(self.package, inside, self.keys)
        with self.assertRaisesRegex(release_signature.SigningError, "refusing to overwrite"):
            release_signature.generate_key(self.key)
        # A key Forge does not embed is refused before anything is written.
        other = release_signature.generate_key(self.root / "keys" / "other.pem")
        with self.assertRaisesRegex(release_signature.SigningError, "not in"):
            release_signature.sign_package(self.package, self.root / "keys" / "other.pem", self.keys)
        self.assertFalse((self.package / "docs/release-signature.json").exists())
        release_signature.add_key(other, self.keys)
        self.assertEqual(set(release_signature.load_keys(self.keys)), {self.entry["key_id"], other["key_id"]})

    def test_the_embedded_key_list_is_valid(self):
        release_signature.load_keys()


if __name__ == "__main__":
    unittest.main()
