import unittest
from pathlib import Path
from analyze_aot_stacks import summarize


class AttributionTests(unittest.TestCase):
    def data(self, stacks):
        return {"samples": sum(s["count"] for s in stacks), "stacks": stacks,
                "modules": [{"path": str(Path("aot.dll").resolve()), "base": 0x10000, "size": 0x1000},
                            {"path": str(Path("host.exe").resolve()), "base": 0x20000, "size": 0x1000}]}

    def test_return_boundary_and_folded_helper_do_not_invent_owner(self):
        entries = [(0x100, "??$ReadFast@I@Execute_wrong_00FFFFFF@"),
                   (0x200, "?Execute_Work_00123456@"),
                   (0x300, "?Execute_Next_00654321@")]
        result = summarize(self.data([{"pcs": [0x10108, 0x10300], "count": 7}]),
                           Path("aot.dll"), entries)
        self.assertEqual(result["direct_totals"], {"00123456": 7})
        self.assertEqual(result["aot_categories"], {"memory_read_helper": 7})

    def test_host_leaf_is_not_charged_to_aot_and_recursion_not_double_counted(self):
        entries = [(0x100, "?Execute_Work_00123456@")]
        result = summarize(self.data([{"pcs": [0x20100, 0x10108], "count": 5},
                                     {"pcs": [0x10108, 0x10120], "count": 3}]),
                           Path("aot.dll"), entries)
        self.assertEqual(result["aot_leaf_samples"], 3)
        self.assertEqual(result["inclusive_owners"], {"00123456": 3})

    def test_unresolved_helper_is_not_attached_to_an_arbitrary_owner(self):
        entries = [(0x100, "??$ReadFast@I")]
        result = summarize(self.data([{"pcs": [0x10108], "count": 4}]), Path("aot.dll"), entries)
        self.assertEqual(result["aot_without_translated_owner"], 4)
        self.assertFalse(result["direct_totals"])

    def test_wrong_module_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "absent"):
            summarize(self.data([]), Path("different.dll"), [])


if __name__ == "__main__":
    unittest.main()
