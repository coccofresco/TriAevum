import unittest

from qualify_shader_handoff import shader_statistics


class ShaderHandoffStatisticsTests(unittest.TestCase):
    log = ("TRIAEVUM_SPIRV_CACHE requests=0 hits=0 misses=0 rejected=0 compiled=0 "
           "compile_failed=0 writes=0 write_failed=0 compile_ms=0.0\n"
           "OOT3D_PICA_AOT_SHADER_RESOLUTION hits=98 misses=0 strict=0 entries=889\n")

    def test_unmeasured_is_not_zero(self):
        stats = shader_statistics(self.log)
        self.assertEqual(stats["compiled"], 0)
        self.assertIsNone(stats["all_pass_compiled"])

    def test_audit_exposes_compilation_outside_cache(self):
        stats = shader_statistics(self.log + "TRIAEVUM_SHADERC_AUDIT calls=20 compile_ms=2900.0\n")
        self.assertEqual(stats["compiled"], 0)
        self.assertEqual(stats["all_pass_compiled"], 20)

    def test_missing_duplicate_or_failed_counters_are_not_accepted(self):
        for log in ("", self.log + self.log, self.log.replace("compile_failed=0", "compile_failed=1")):
            with self.assertRaises(ValueError):
                shader_statistics(log)


if __name__ == "__main__":
    unittest.main()
