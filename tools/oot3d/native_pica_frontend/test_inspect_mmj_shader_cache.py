import unittest

from inspect_mmj_shader_cache import describe, parse_corpus, parse_metadata


FIXTURE = """// shader: 8B31, 1
void main() {}
// reference: A, 1
// shader: 8B30, 2
void main() {}
// reference: B, 2
// program: 1, 0, 2
"""
META = "version: 8\nshader: 2\nreference: 2\nprogram: 1\n"


class MmjCacheTests(unittest.TestCase):
    def test_reads_glsl_without_promoting_foreign_identity(self):
        corpus = parse_corpus(FIXTURE, META)
        report = describe(corpus)
        self.assertEqual(report["stage_counts"], {"vertex": 1, "fragment": 1, "geometry": 0})
        self.assertEqual(report["unique_stage_sources"], 2)
        self.assertEqual(corpus.programs, [(1, 0, 2)])
        self.assertEqual(report["canonical_shader_modules_added"], 0)
        self.assertFalse(report["native_pica_registers_available"])
        self.assertFalse(report["runtime_binding_validated"])

    def test_line_endings_do_not_change_source_identity(self):
        first = parse_corpus(FIXTURE, META)
        second = parse_corpus(FIXTURE.replace("\n", "\r\n"), META)
        self.assertEqual(first.shaders, second.shaders)

    def test_version_26_does_not_claim_same_abi(self):
        corpus = parse_corpus(FIXTURE, META.replace("version: 8", "version: 26"))
        self.assertEqual(corpus.metadata["version"], 26)

    def test_rejects_metadata_mismatch_or_unknown_version(self):
        for metadata in (META.replace("shader: 2", "shader: 3"),
                         META.replace("version: 8", "version: 99"),
                         META + "shader: 2\n", META + "extra: 0\n", "version: 8"):
            with self.subTest(metadata=metadata), self.assertRaises(ValueError):
                parse_corpus(FIXTURE, metadata)

    def test_rejects_incomplete_references_or_programs(self):
        for text in (FIXTURE.replace("A, 1", "A, F"),
                     FIXTURE.replace("1, 0, 2", "1, 3, 2"),
                     FIXTURE.replace("1, 0, 2", "2, 0, 1"),
                     FIXTURE.replace("B, 2", "A, 2")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_corpus(text, META)

    def test_rejects_malformed_markers(self):
        for text in (FIXTURE.replace("8B31, 1", "8B31, not-hex"),
                     FIXTURE.replace("8B31", "FFFF"),
                     FIXTURE.replace("// shader: ", "// shader:", 1),
                     FIXTURE.replace("8B31, 1", "8B31, 10000000000000000"),
                     "outside\n" + FIXTURE):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_corpus(text, META)

    def test_collision_does_not_replace_source(self):
        duplicate = FIXTURE.replace("8B30, 2", "8B30, 1")
        with self.assertRaisesRegex(ValueError, "collision"):
            parse_corpus(duplicate, META)

    def test_reports_stub_without_claiming_it_is_executed(self):
        text = FIXTURE.replace("// reference: B, 2",
                               "vec4 shadowTexture(vec2 uv, float w) { return vec4(1.0); }\n"
                               "// reference: B, 2")
        report = describe(parse_corpus(text, META))
        fragment = report["shaders"][1]
        self.assertEqual(fragment["white_shadow_helper_definitions"], ["shadowTexture"])
        self.assertEqual(fragment["shadow_helper_name_occurrences"], {"shadowTexture": 1})

    def test_metadata_count_is_bounded(self):
        with self.assertRaises(ValueError):
            parse_metadata(META.replace("shader: 2", "shader: 999999999"))


if __name__ == "__main__":
    unittest.main()
