"""Tests for revision-pinned source owner closure generation."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_owner_closure import (
    build_consumer_work_request,
    build_delta,
    build_decomp_handoff,
    build_decomp_work_request,
    build_snapshot,
    load_previous,
    render_report,
    write_snapshot,
)


def git(repository: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


class OwnerClosureFixture:
    def __init__(self, root: Path, missing_dependencies: bool) -> None:
        self.decomp = root / "decomp"
        self.runtime = root / "runtime"
        self.decomp.mkdir()
        self.runtime.mkdir()

        for relative in (
            "config",
            "symbols",
            "metadata",
            "src",
            "analysis",
        ):
            (self.decomp / relative).mkdir()
        (self.runtime / "profiles").mkdir()
        (self.runtime / "runtime").mkdir()

        semantic_files = ["src/foo.c"]
        if not missing_dependencies:
            semantic_files.append("src/bar.c")
        bulk_files = ["src/bulk.c"] if missing_dependencies else []
        (self.decomp / "config" / "source_lanes.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "lanes": {
                        "portable_semantic": {
                            "source_files": semantic_files,
                            "entry_manifests": [],
                        },
                        "bulk_recovered_c": {
                            "source_files": bulk_files,
                            "manifests": (
                                ["metadata/bulk.csv"]
                                if missing_dependencies
                                else []
                            ),
                        },
                    },
                }
            ),
            encoding="utf-8",
        )
        reviewed_count = 1 if missing_dependencies else 2
        (self.decomp / "config" / "reconstruction.json").write_text(
            json.dumps(
                {
                    "checkpoint": {
                        "automated_function_coverage": 3,
                        "target_linked_c_bodies": 3,
                        "reviewed_target_linked_c_bodies": reviewed_count,
                        "bulk_recovered_target_linked_c_bodies": 3
                        - reviewed_count,
                    }
                }
            ),
            encoding="utf-8",
        )
        (self.decomp / "symbols" / "manual_symbols.csv").write_text(
            "entry,old_name,new_name,kind,confidence,source_file,notes\n"
            "00100000,FUN_00100000,Foo,function,high,src/foo.c,fixture\n"
            "00100100,FUN_00100100,Bar,function,high,src/bar.c,fixture\n"
            "00100200,FUN_00100200,Baz,function,high,src/bulk.c,fixture\n",
            encoding="utf-8",
        )
        (self.decomp / "metadata" / "exact_functions.csv").write_text(
            "entry,name,former_source\n", encoding="utf-8"
        )
        (self.decomp / "src" / "foo.c").write_text(
            "void Foo(void) { Bar(); }\n", encoding="utf-8"
        )
        if missing_dependencies:
            (self.decomp / "src" / "bulk.c").write_text(
                "void Bar(void) {}\nvoid Baz(void) {}\n", encoding="utf-8"
            )
            (self.decomp / "metadata" / "bulk.csv").write_text(
                "entry,name,cohort_source\n"
                "00100100,Bar,src/bulk.c\n"
                "00100200,Baz,src/bulk.c\n",
                encoding="utf-8",
            )
        else:
            (self.decomp / "src" / "bar.c").write_text(
                "void Bar(void) {}\n", encoding="utf-8"
            )

        nodes = [
            {
                "entry": "00100000",
                "name": "Foo",
                "signature": "void Foo(void)",
                "caller_count": 0,
            },
            {
                "entry": "00100100",
                "name": "Bar",
                "signature": "void Bar(void)",
                "caller_count": 1,
            },
            {
                "entry": "00100200",
                "name": "Baz",
                "signature": "void Baz(void)",
                "caller_count": 0,
            },
        ]
        (self.decomp / "analysis" / "callgraph.json").write_text(
            json.dumps(
                {
                    "nodes": nodes,
                    "edges": [{"caller": "00100000", "callee": "00100100"}],
                }
            ),
            encoding="utf-8",
        )
        if missing_dependencies:
            (self.decomp / "analysis" / "function_pointer_refs.csv").write_text(
                "source_entry,source_name,source_file,line,ref_name,"
                "literal_address,pointer_value,target_entry,target_name,"
                "thumb_adjusted,context\n"
                "00100000,Foo,src/foo.c,1,DAT_1,00100080,00100200,"
                "00100200,Baz,False,callback = DAT_1;\n",
                encoding="utf-8",
            )

        (self.runtime / "runtime" / "registry.h").write_text(
            "constexpr unsigned kFooEntry = 0x00100000U;\n",
            encoding="utf-8",
        )
        (self.runtime / "runtime" / "registry.cpp").write_text(
            "constexpr std::array kTypedEntries{ kFooEntry };\n",
            encoding="utf-8",
        )
        (self.runtime / "profiles" / "fixture.json").write_text(
            json.dumps(
                {
                    "format": "oot3d_hot_source_coverage_v1",
                    "functions": [
                        {
                            "entry": 0x00100100,
                            "total_profile_coverage": 0.25,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        self.config = self.runtime / "owners.json"
        self.config.write_text(
            json.dumps(
                {
                    "format": "oot3d_source_owner_roots_v1",
                    "profiles": [
                        {
                            "id": "fixture",
                            "path": "profiles/fixture.json",
                            "weight": 1.0,
                        }
                    ],
                    "runtime_entry_registries": [
                        {
                            "id": "fixture",
                            "header": "runtime/registry.h",
                            "source": "runtime/registry.cpp",
                            "array": "kTypedEntries",
                        }
                    ],
                    "service_boundaries": [],
                    "owners": [
                        {
                            "id": "foo",
                            "entry": "0x00100000",
                            "expected_name": "Foo",
                            "domain": "fixture",
                            "priority": 100,
                        }
                    ],
                    "manual_blockers": [],
                    "indirect_contracts": [],
                    "approved_owners": [],
                    "bundle_support_paths": [],
                }
            ),
            encoding="utf-8",
        )

        (self.decomp / ".gitignore").write_text("analysis/\n", encoding="utf-8")
        git(self.decomp, "init")
        git(self.decomp, "config", "user.email", "fixture@example.invalid")
        git(self.decomp, "config", "user.name", "Fixture")
        git(self.decomp, "add", ".")
        git(self.decomp, "commit", "-m", "fixture")
        self.revision = git(self.decomp, "rev-parse", "HEAD")


@unittest.skipIf(shutil.which("git") is None, "git is required")
class OwnerClosureTests(unittest.TestCase):
    def test_tracks_direct_and_indirect_owner_blockers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), True)
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )

        owner = snapshot["owners"][0]
        self.assertEqual(
            snapshot["source"]["sidecar_checkout_revision"],
            fixture.revision,
        )
        self.assertTrue(
            snapshot["source"]["pinned_revision_matches_sidecar_checkout"]
        )
        self.assertFalse(owner["source_graph_closed"])
        self.assertEqual(owner["direct_status_counts"], {"bulk_only": 1})
        self.assertEqual(owner["unresolved_indirect_edges"], 1)
        worklist = {item["name"]: item for item in snapshot["worklist"]}
        self.assertEqual(worklist["Bar"]["relations"], ["direct_callgraph"])
        self.assertEqual(worklist["Bar"]["direct_owner_count"], 1)
        self.assertEqual(worklist["Baz"]["relations"], ["indirect_target"])
        self.assertGreater(
            worklist["Bar"]["weighted_profile_heat"],
            worklist["Baz"]["weighted_profile_heat"],
        )
        self.assertEqual(snapshot["runtime_typed_entry_count"], 1)
        coverage = snapshot["owner_closure_coverage"]
        self.assertEqual(
            coverage["owner_memberships"],
            {"resolved": 1, "total": 2, "open": 1, "percent": 50.0},
        )
        self.assertEqual(
            coverage["unique_entries"],
            {"resolved": 1, "total": 2, "open": 1, "percent": 50.0},
        )
        self.assertEqual(coverage["source_closed_owner_count"], 0)

    def test_exact_platform_boundary_closes_source_not_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), True)
            config = json.loads(
                fixture.config.read_text(encoding="utf-8")
            )
            config["service_boundaries"] = [
                {
                    "id": "fixture_platform_service",
                    "name_patterns": [],
                    "entries": ["0x00100100"],
                    "rationale": (
                        "Exact target-proven platform service; source "
                        "closure does not imply a native runtime adapter."
                    ),
                }
            ]
            config["indirect_contracts"] = [
                {
                    "source": "0x00100000",
                    "target": "0x00100200",
                }
            ]
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )

        owner = snapshot["owners"][0]
        bar = next(
            member
            for member in owner["members"]
            if member["entry"] == "0x00100100"
        )
        self.assertTrue(owner["source_graph_closed"])
        self.assertEqual(bar["source_status"], "service_boundary")
        self.assertEqual(
            bar["service_boundary"], "fixture_platform_service"
        )
        self.assertFalse(owner["approved_for_runtime_import"])
        self.assertFalse(owner["runtime_import_ready"])
        self.assertEqual(snapshot["worklist"], [])

    def test_reads_committed_source_and_materializes_closed_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            (fixture.decomp / "src" / "foo.c").write_text(
                "void Foo(void) { user_uncommitted_edit(); }\n",
                encoding="utf-8",
            )
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            self.assertTrue(snapshot["owners"][0]["source_graph_closed"])
            self.assertEqual(
                snapshot["source"]["tracked_worktree_changes_ignored"], 1
            )
            output = Path(directory) / "output"
            package = write_snapshot(
                snapshot,
                output,
                fixture.decomp,
            )
            repeated_package = write_snapshot(
                snapshot,
                output,
                fixture.decomp,
            )
            copied = (
                package
                / "owners"
                / "foo"
                / "source_candidate"
                / "src"
                / "foo.c"
            ).read_text(encoding="utf-8")
            work_order = json.loads(
                (
                    package
                    / "owners"
                    / "foo"
                    / "runtime_import_work_order.json"
                ).read_text(encoding="utf-8")
            )
            report = package / "report.md"
            report.write_text("tampered\n", encoding="utf-8")
            with self.assertRaisesRegex(
                ValueError, "artifact hash mismatch"
            ):
                write_snapshot(
                    snapshot,
                    output,
                    fixture.decomp,
                )

        self.assertEqual(package, repeated_package)
        self.assertIn("Bar();", copied)
        self.assertNotIn("user_uncommitted_edit", copied)
        self.assertEqual(work_order["owner"], "foo")
        self.assertEqual(
            work_order["runtime_activation"]["state"],
            "acceptance_required",
        )
        self.assertEqual(
            work_order["source_closure"]["function_count"], 2
        )
        self.assertEqual(
            work_order["source_candidate"]["materialized_file_count"], 2
        )
        self.assertRegex(
            work_order["source_candidate"]["tree_sha256"],
            r"^[0-9a-f]{64}$",
        )

    def test_snapshot_identity_covers_runtime_consumer_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            before = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            registry_source = fixture.runtime / "runtime" / "registry.cpp"
            registry_source.write_text(
                registry_source.read_text(encoding="utf-8")
                + "// consumer revision\n",
                encoding="utf-8",
            )
            after = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )

        self.assertNotEqual(before["snapshot_id"], after["snapshot_id"])
        self.assertNotEqual(
            before["consumer_input_sha256"],
            after["consumer_input_sha256"],
        )

    def test_semantic_certified_bulk_remains_a_cleanup_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), True)
            before = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            lanes_path = fixture.decomp / "config" / "source_lanes.json"
            lanes = json.loads(lanes_path.read_text(encoding="utf-8"))
            lanes["lanes"]["semantic_certified_bulk_c"] = {
                "manifest": "metadata/certified.csv"
            }
            lanes_path.write_text(json.dumps(lanes), encoding="utf-8")
            (fixture.decomp / "metadata" / "certified.csv").write_text(
                "entry,name,n64_path,n64_signature,target_source,"
                "cohort_source,recovered_definition,target_lines,"
                "runtime_priority,parity,dependency_ratio,body_status,"
                "semantic_status,remaining_work\n"
                "00100100,Bar,src/bar.c,void Bar(void),target.c,"
                "src/bulk.c,Bar,12,true,0.9,1.0,"
                "compile-qualified-bulk-c,target-n64-certified,"
                "local-type-and-expression-cleanup\n",
                encoding="utf-8",
            )
            reconstruction_path = (
                fixture.decomp / "config" / "reconstruction.json"
            )
            reconstruction = json.loads(
                reconstruction_path.read_text(encoding="utf-8")
            )
            reconstruction["checkpoint"][
                "semantically_certified_bulk_c_bodies"
            ] = 1
            reconstruction["checkpoint"][
                "semantic_certified_manifest_entries"
            ] = 1
            reconstruction_path.write_text(
                json.dumps(reconstruction), encoding="utf-8"
            )
            git(fixture.decomp, "add", ".")
            git(fixture.decomp, "commit", "-m", "certify Bar")
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                git(fixture.decomp, "rev-parse", "HEAD"),
            )
            request = build_decomp_work_request(snapshot)
            delta = build_delta(snapshot, before)

        owner = snapshot["owners"][0]
        bar = next(
            row for row in snapshot["registry"] if row["name"] == "Bar"
        )
        self.assertEqual(
            bar["source_status"], "semantic_certified_bulk"
        )
        self.assertEqual(snapshot["semantic_certified_bulk_count"], 1)
        self.assertFalse(owner["source_graph_closed"])
        self.assertEqual(
            request["semantic_certified_cleanup"][0]["entry"],
            "0x00100100",
        )
        self.assertEqual(
            request["semantic_certified_cleanup"][0]["remaining_work"],
            "local-type-and-expression-cleanup",
        )
        self.assertEqual(
            delta["certification_promotions"][0]["entry"],
            "0x00100100",
        )
        self.assertEqual(delta["semantic_promotions"], [])

    def test_reviewed_promotion_supersedes_historical_certification(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), True)
            lanes_path = fixture.decomp / "config" / "source_lanes.json"
            lanes = json.loads(lanes_path.read_text(encoding="utf-8"))
            lanes["lanes"]["portable_semantic"]["source_files"].append(
                "src/reviewed_bar.c"
            )
            lanes["lanes"]["semantic_certified_bulk_c"] = {
                "manifest": "metadata/certified.csv"
            }
            lanes_path.write_text(json.dumps(lanes), encoding="utf-8")
            (fixture.decomp / "src" / "reviewed_bar.c").write_text(
                "void Bar(void) {}\n", encoding="utf-8"
            )
            (fixture.decomp / "metadata" / "certified.csv").write_text(
                "entry,name,cohort_source\n"
                "00100100,Bar,src/retired_bulk.c\n",
                encoding="utf-8",
            )
            reconstruction_path = (
                fixture.decomp / "config" / "reconstruction.json"
            )
            reconstruction = json.loads(
                reconstruction_path.read_text(encoding="utf-8")
            )
            reconstruction["checkpoint"][
                "semantic_certified_manifest_entries"
            ] = 1
            reconstruction_path.write_text(
                json.dumps(reconstruction), encoding="utf-8"
            )
            git(fixture.decomp, "add", ".")
            git(fixture.decomp, "commit", "-m", "promote Bar")
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                git(fixture.decomp, "rev-parse", "HEAD"),
            )

        bar = next(row for row in snapshot["registry"] if row["name"] == "Bar")
        self.assertEqual(bar["source_status"], "reviewed_semantic")
        self.assertEqual(snapshot["semantic_certified_bulk_count"], 0)
        self.assertEqual(
            snapshot["owners"][0]["direct_status_counts"],
            {"reviewed_semantic": 1},
        )

    def test_previous_snapshot_uses_nearest_ancestor_revision(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            output = Path(directory) / "output"
            write_snapshot(snapshot, output, fixture.decomp)

            (fixture.decomp / "README.md").write_text(
                "next semantic checkpoint\n", encoding="utf-8"
            )
            git(fixture.decomp, "add", "README.md")
            git(fixture.decomp, "commit", "-m", "next")
            current = git(fixture.decomp, "rev-parse", "HEAD")
            current_snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                current,
            )
            previous = load_previous(
                output,
                fixture.decomp,
                current_snapshot,
            )

        self.assertIsNotNone(previous)
        assert previous is not None
        self.assertEqual(previous["snapshot_id"], snapshot["snapshot_id"])
        self.assertEqual(len(previous["registry"]), 3)

    def test_previous_snapshot_prefers_matching_consumer_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            matching = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            output = Path(directory) / "output"
            write_snapshot(matching, output, fixture.decomp)

            config = json.loads(
                fixture.config.read_text(encoding="utf-8")
            )
            config["owners"][0]["priority"] = 99
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            alternate = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            write_snapshot(alternate, output, fixture.decomp)

            config["owners"][0]["priority"] = 100
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            (fixture.decomp / "README.md").write_text(
                "next semantic checkpoint\n", encoding="utf-8"
            )
            git(fixture.decomp, "add", "README.md")
            git(fixture.decomp, "commit", "-m", "next")
            current = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                git(fixture.decomp, "rev-parse", "HEAD"),
            )
            previous = load_previous(output, fixture.decomp, current)

        self.assertIsNotNone(previous)
        assert previous is not None
        self.assertEqual(previous["snapshot_id"], matching["snapshot_id"])

    def test_target_mismatch_quarantines_reviewed_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            config = json.loads(fixture.config.read_text(encoding="utf-8"))
            config["source_status_overrides"] = [
                {
                    "entry": "0x00100100",
                    "status": "target_mismatch",
                    "kind": "fixture_abi",
                    "description": "Target evidence disagrees.",
                    "evidence": "fixture:1",
                }
            ]
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            request = build_decomp_work_request(snapshot)

        owner = snapshot["owners"][0]
        bar = next(
            row for row in snapshot["registry"] if row["name"] == "Bar"
        )
        self.assertFalse(owner["source_graph_closed"])
        self.assertEqual(bar["original_source_status"], "reviewed_semantic")
        self.assertEqual(bar["source_status"], "target_mismatch")
        self.assertEqual(request["target_mismatches"][0]["name"], "Bar")

    def test_resolved_manual_finding_is_preserved_for_handoff(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            config = json.loads(fixture.config.read_text(encoding="utf-8"))
            config["manual_blockers"] = [
                {
                    "id": "split_foo_tail",
                    "owner": "foo",
                    "entry": "0x00100000",
                    "kind": "function_boundary",
                    "status": "resolved",
                    "description": "The function boundary was fused.",
                    "resolution": "Split the tail-called helper at 0x00100200.",
                    "evidence": ["fixture disassembly"],
                }
            ]
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            request = build_decomp_work_request(snapshot)
            report = render_report(snapshot, None)

        self.assertEqual(request["manual_blockers"], [])
        self.assertEqual(
            request["resolved_structural_findings"][0]["id"],
            "split_foo_tail",
        )
        self.assertIn(
            "Split the tail-called helper at 0x00100200.", report
        )

    def test_resolved_owner_identity_supersedes_stale_callgraph_name(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            config = json.loads(fixture.config.read_text(encoding="utf-8"))
            owner = config["owners"][0]
            owner["expected_name"] = "CanonicalFoo"
            owner["identity_resolution"] = {
                "status": "resolved",
                "resolved_name": "CanonicalFoo",
                "description": "Target evidence supersedes the callgraph name.",
                "evidence": ["fixture target evidence"],
            }
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            request = build_consumer_work_request(snapshot)
            report = render_report(snapshot, None)

        owner = snapshot["owners"][0]
        self.assertEqual(owner["actual_name"], "Foo")
        self.assertEqual(owner["canonical_name"], "CanonicalFoo")
        self.assertFalse(owner["callgraph_identity_matches_expected"])
        self.assertTrue(owner["identity_matches_expected"])
        self.assertEqual(
            request["owner_frontier"][0]["name"], "CanonicalFoo"
        )
        self.assertEqual(
            request["owner_frontier"][0]["callgraph_name"], "Foo"
        )
        self.assertIn("`CanonicalFoo` (callgraph `Foo`)", report)

    def test_dynamic_dispatch_contract_blocks_until_implemented(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            baseline = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            config = json.loads(
                fixture.config.read_text(encoding="utf-8")
            )
            config["dynamic_dispatch_contracts"] = [
                {
                    "id": "fixture_callback",
                    "owner": "foo",
                    "source": "0x00100000",
                    "kind": "fixture_function_pointer",
                    "status": "open",
                    "callback_fields": [
                        {
                            "name": "update",
                            "instance_offset": "0x10",
                        }
                    ],
                    "description": "Dispatch a fixture callback.",
                    "evidence": ["fixture target evidence"],
                }
            ]
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            open_snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            request = build_consumer_work_request(open_snapshot)
            delta = build_delta(open_snapshot, baseline)

            config["dynamic_dispatch_contracts"][0].update(
                {
                    "status": "implemented",
                    "implementation": "runtime/fixture_dispatch.cpp",
                    "verification": ["fixture callback differential"],
                }
            )
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            implemented_snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )

        open_owner = open_snapshot["owners"][0]
        self.assertFalse(open_owner["source_graph_closed"])
        self.assertEqual(
            open_owner["unresolved_dynamic_dispatch_contracts"], 1
        )
        self.assertEqual(
            request["dynamic_dispatch_contracts"][0]["id"],
            "fixture_callback",
        )
        self.assertFalse(
            request["dynamic_dispatch_contracts"][0]["resolved"]
        )
        self.assertEqual(
            delta["dynamic_dispatch_contract_changes"],
            [
                {
                    "id": "fixture_callback",
                    "owner": "foo",
                    "previous_status": None,
                    "current_status": "open",
                }
            ],
        )
        implemented_owner = implemented_snapshot["owners"][0]
        self.assertTrue(implemented_owner["source_graph_closed"])
        self.assertEqual(
            implemented_owner["unresolved_dynamic_dispatch_contracts"], 0
        )

    def test_consumer_reviewed_overlay_closes_and_materializes_owner(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), True)
            implementation = fixture.runtime / "runtime" / "bar_owner.cpp"
            verification = fixture.runtime / "runtime" / "bar_owner_tests.cpp"
            implementation.write_text(
                "void Bar(void) {}\n", encoding="utf-8"
            )
            verification.write_text(
                "void TestBarOwnerDifferential(void) {}\n",
                encoding="utf-8",
            )
            overlay_path = fixture.runtime / "consumer_overlays.json"
            overlay_path.write_text(
                json.dumps(
                    {
                        "format": "oot3d_consumer_semantic_overlays_v1",
                        "entries": [
                            {
                                "id": "bar_owner",
                                "entry": "0x00100100",
                                "name": "Bar",
                                "status": "reviewed_semantic",
                                "implementation_files": [
                                    "runtime/bar_owner.cpp"
                                ],
                                "verification_files": [
                                    "runtime/bar_owner_tests.cpp"
                                ],
                                "verification_cases": [
                                    "TestBarOwnerDifferential"
                                ],
                                "abi": {"prototype": "void Bar(void)"},
                                "layout": [{"field": "none", "offset": "0x0"}],
                                "target_evidence": ["fixture disassembly"],
                                "decomp_handoff": {
                                    "suggested_header_path": "include/bar.h",
                                    "suggested_source_path": "src/bar.c",
                                    "semantic_body": ["void Bar(void) {}"],
                                    "required_assertions": [
                                        "Bar has no arguments"
                                    ],
                                },
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            config = json.loads(
                fixture.config.read_text(encoding="utf-8")
            )
            config["consumer_semantic_overlay_manifest"] = (
                "consumer_overlays.json"
            )
            config["indirect_contracts"] = [
                {
                    "source": "0x00100000",
                    "target": "0x00100200",
                }
            ]
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )

            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )
            output = Path(directory) / "output"
            package = write_snapshot(
                snapshot, output, fixture.decomp
            )
            handoff = build_decomp_handoff(snapshot)
            materialized = (
                package
                / "owners"
                / "foo"
                / "source_candidate"
                / "runtime"
                / "bar_owner.cpp"
            ).is_file()
            handoff_written = (package / "decomp_handoff.md").is_file()

            lanes_path = fixture.decomp / "config" / "source_lanes.json"
            lanes = json.loads(lanes_path.read_text(encoding="utf-8"))
            lanes["lanes"]["portable_semantic"]["source_files"].append(
                "src/bulk.c"
            )
            lanes_path.write_text(json.dumps(lanes), encoding="utf-8")
            git(fixture.decomp, "add", "config/source_lanes.json")
            git(fixture.decomp, "commit", "-m", "review Bar in producer")
            promoted = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                git(fixture.decomp, "rev-parse", "HEAD"),
            )
            promoted_handoff = build_decomp_handoff(promoted)
            promoted_delta = build_delta(promoted, snapshot)

        owner = snapshot["owners"][0]
        bar = next(
            row for row in snapshot["registry"] if row["name"] == "Bar"
        )
        self.assertEqual(
            bar["producer_source_status"], "bulk_only"
        )
        self.assertEqual(
            bar["source_status"], "consumer_reviewed_semantic"
        )
        self.assertTrue(owner["source_graph_closed"])
        self.assertEqual(
            handoff["entries"][0]["handoff_status"],
            "ready_for_producer_review",
        )
        self.assertTrue(materialized)
        self.assertTrue(handoff_written)
        promoted_bar = next(
            row for row in promoted["registry"] if row["name"] == "Bar"
        )
        self.assertEqual(
            promoted_bar["source_status"], "reviewed_semantic"
        )
        self.assertEqual(
            promoted_handoff["entries"][0]["handoff_status"],
            "producer_already_reviewed",
        )
        self.assertEqual(
            promoted_delta["consumer_semantic_superseded"][0]["entry"],
            "0x00100100",
        )

    def test_owner_import_requires_hashed_acceptance_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = OwnerClosureFixture(Path(directory), False)
            config = json.loads(
                fixture.config.read_text(encoding="utf-8")
            )
            config["approved_owners"] = ["foo"]
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            with self.assertRaisesRegex(
                ValueError, "approved owner lacks import acceptance"
            ):
                build_snapshot(
                    fixture.decomp,
                    fixture.runtime,
                    fixture.config,
                    fixture.revision,
                )

            config["owner_import_acceptance"] = [
                {
                    "owner": "foo",
                    "status": "approved",
                    "implementation_files": [
                        "runtime/registry.h",
                        "runtime/registry.cpp",
                    ],
                    "verification_files": [
                        "runtime/registry.cpp"
                    ],
                    "verification_cases": [
                        "TestFooOwnerDifferential"
                    ],
                    "target_evidence": [
                        "fixture target instructions"
                    ],
                    "abi_layout": "No arguments or guest layout.",
                    "native30_differential": (
                        "Typed and reference paths match."
                    ),
                    "savestate_contract": (
                        "No state outside captured guest state."
                    ),
                    "callback_coverage": "No dynamic callbacks.",
                    "enhanced60_timing": (
                        "One call per guest simulation update."
                    ),
                    "performance_scope": (
                        "The owner no longer executes A32 blocks."
                    ),
                }
            ]
            fixture.config.write_text(
                json.dumps(config), encoding="utf-8"
            )
            snapshot = build_snapshot(
                fixture.decomp,
                fixture.runtime,
                fixture.config,
                fixture.revision,
            )

        owner = snapshot["owners"][0]
        acceptance = owner["runtime_import_acceptance"]
        self.assertTrue(owner["runtime_import_ready"])
        self.assertEqual(acceptance["status"], "approved")
        self.assertEqual(
            snapshot["owner_import_acceptance"]["entry_count"], 1
        )
        self.assertIn(
            "runtime/registry.cpp", acceptance["file_sha256"]
        )


if __name__ == "__main__":
    unittest.main()
