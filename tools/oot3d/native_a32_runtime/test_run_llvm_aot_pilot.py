from __future__ import annotations

import unittest

from run_llvm_aot_pilot import _extract_function, _state_access_metrics


class LlvmAotPilotTests(unittest.TestCase):
    def test_extracts_function_and_tracks_guest_pointer_accesses(self) -> None:
        ir = """define void @other() {
  ret void
}
define void @Execute_Test(ptr %frame) {
entry:
  %guest = load ptr, ptr %frame, align 8
  %r0 = getelementptr i8, ptr %guest, i64 0
  %value = load i32, ptr %r0, align 4
  store i32 %value, ptr %r0, align 4
  ret void
}
"""
        function = _extract_function(ir, "Execute_Test")
        metrics = _state_access_metrics(function)
        self.assertEqual(metrics["guest_pointer_values"], 2)
        self.assertEqual(metrics["guest_loads"], 1)
        self.assertEqual(metrics["guest_stores"], 1)

    def test_tracks_promoted_state_separately(self) -> None:
        ir = """define void @Execute_Promoted(ptr %frame, ptr %state) {
entry:
  %r0 = getelementptr i8, ptr %state, i64 0
  %value = load i32, ptr %r0, align 4
  store i32 %value, ptr %r0, align 4
  ret void
}
"""
        metrics = _state_access_metrics(_extract_function(ir, "Execute_Promoted"))
        self.assertEqual(metrics["guest_loads"], 0)
        self.assertEqual(metrics["promoted_loads"], 1)
        self.assertEqual(metrics["promoted_stores"], 1)


if __name__ == "__main__":
    unittest.main()
