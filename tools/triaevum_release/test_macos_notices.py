from pathlib import Path
import tempfile
import unittest

from macos_forge_package import (EXCLUDED_REFERENCE_LICENSES, SOURCE_DOCUMENTS,
                                reference_license_documents, verified_source_documents)
from macos_notices import LICENSE_SHA256, PROVENANCE, notice_errors, verified_notices
from test_macos_private_content import source_fixture


ROOT = Path(__file__).resolve().parents[2]


class MacOSNoticesTests(unittest.TestCase):
    def test_reference_licenses_resolve_from_verified_source_and_omit_unshipped_sdk(self):
        names = ("GPL-2.0-or-later.txt", "iMMERSE-LICENSE.txt", "iMMERSE-MXAO.md",
                 "libultraship-MIT.txt", "nihstro-BSD-3-Clause.txt")
        files = {"LICENSES/" + name: (ROOT / "LICENSES" / name).read_bytes() for name in names}
        selected = {name: ("license", Path("unused-reference-copy"))
                    for name in (*files, *EXCLUDED_REFERENCE_LICENSES)}
        selected["data/recipe.json"] = ("recipe", Path("unused-reference-data"))
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "source.zip"
            source_fixture(archive, files)
            documents = verified_source_documents(archive, "a" * 40)
            self.assertEqual(reference_license_documents(selected, documents), files)
            # Even if present in the source, notices for the absent SDK stay out.
            documents.update({name: b"unused" for name in EXCLUDED_REFERENCE_LICENSES})
            self.assertEqual(reference_license_documents(selected, documents), files)

    def test_reference_license_omissions_fail_explicitly_before_staging(self):
        missing = ("LICENSES/missing-a.txt", "LICENSES/missing-b.txt")
        selected = {name: ("license", Path("unused-reference-copy")) for name in missing}
        with self.assertRaises(ValueError) as raised:
            reference_license_documents(selected, {})
        for name in missing:
            self.assertIn(name, str(raised.exception))

    def test_checked_in_license_snapshots_and_source_bound_copy(self):
        files = {name: (ROOT / name).read_bytes() for name in (*LICENSE_SHA256, PROVENANCE)}
        self.assertEqual(list(notice_errors(files.__getitem__)), [])
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "source.zip"
            source_fixture(archive, files)
            documents = verified_source_documents(archive, "a" * 40)
            result = verified_notices(documents, ROOT / "LICENSES/MoltenVK-Apache-2.0.txt",
                                      ROOT / "LICENSES/Vulkan-Loader-LICENSE.txt")
            self.assertEqual(result, files)
            for name in ("TRIAEVUM_CONTRIBUTIONS", "AZAHAR_AUDIO_PROVENANCE",
                         "TRIAEVUM_RELEASE_APPROVAL_RECORD", "TRIAEVUM_PRECOMPILED_RELEASE"):
                self.assertIn("docs/" + name + ".md", SOURCE_DOCUMENTS)
                self.assertIn("docs/" + name + ".md", documents)
            replacement = Path(temporary) / "license.txt"
            replacement.write_text("arbitrary Apache-2.0 replacement")
            with self.assertRaisesRegex(ValueError, "differs from verified"):
                verified_notices(documents, replacement, ROOT / "LICENSES/Vulkan-Loader-LICENSE.txt")

    def test_every_missing_or_modified_notice_is_rejected(self):
        files = {name: (ROOT / name).read_bytes() for name in LICENSE_SHA256}
        for name in files:
            for replacement in (None, b"Apache-2.0 placeholder"):
                with self.subTest(name=name, replacement=replacement):
                    changed = dict(files)
                    if replacement is None:
                        del changed[name]
                    else:
                        changed[name] = replacement
                    errors = list(notice_errors(changed.__getitem__))
                    self.assertEqual(len(errors), 1)
                    self.assertIn(name, errors[0])


if __name__ == "__main__":
    unittest.main()
