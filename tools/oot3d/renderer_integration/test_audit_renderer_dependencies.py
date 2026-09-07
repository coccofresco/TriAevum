import tempfile
import unittest
from pathlib import Path

from audit_renderer_dependencies import (
    _cmake_link_graph,
    _evaluate_policy,
    _reachable_link_report,
)


class RendererDependencyAuditTests(unittest.TestCase):
    def test_cmake_alias_is_part_of_reachable_link_closure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(
                """
add_library(three_ds_recomp_runtime STATIC runtime.cpp)
add_library(real_host STATIC host.cpp)
target_link_libraries(real_host PUBLIC three_ds_recomp_runtime)
add_library(runtime_host ALIAS real_host)
add_executable(native_game game.cpp)
target_link_libraries(native_game PRIVATE runtime_host)
""",
                encoding="utf-8",
            )

            report = _reachable_link_report(
                _cmake_link_graph(root), "native_game"
            )

            self.assertEqual(
                report["reachable_targets"],
                ["native_game", "real_host", "runtime_host"],
            )
            self.assertEqual(
                report["runtime_edges"],
                [{"from": "real_host", "to": "three_ds_recomp_runtime"}],
            )

    def test_policy_rejects_forbidden_reachable_target(self) -> None:
        report = {
            "scopes": {},
            "cmake": {
                "runtime_edge_count": 0,
                "reachable_targets": ["native_game", "legacy_host"],
            },
        }
        result = _evaluate_policy(
            report,
            {
                "reachable_runtime_edge_maximum": 0,
                "forbidden_reachable_targets": ["legacy_host"],
            },
        )

        self.assertEqual(result["ratchet_status"], "failed")
        self.assertEqual(
            result["violations"],
            [{"kind": "forbidden_reachable_target",
              "target": "legacy_host"}],
        )


if __name__ == "__main__":
    unittest.main()
