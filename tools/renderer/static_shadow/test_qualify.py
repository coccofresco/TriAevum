"""CPU-only synthetic contracts; no games, assets, renderer or GPU lock."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

import qualify as q


def record(**overrides):
    row = dict.fromkeys(q.FIELDS, 0)
    row.update(v=1, target="1234", owner="0000002100001000", actor="0021", lifetime=7,
               t_first=100, t_last=700, frame_first=1, frame_last=601, observed=601,
               pairs=480, unchanged=480, parts_min=1, parts_max=1, rigid=601, dynamic=601,
               geometry_changes=0, pose_changes=0, material_changes=0)
    row.update(overrides)
    return "[shadow-caster-stability] " + " ".join(f"{k}={v}" for k, v in row.items())


HEADER = "[shadow-caster-trace] v=1 warmup=120 interval=120 clock=unix_ms\n"


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.code = bytes.fromhex("01020304")
        self.common = dict(schema=q.SCHEMA, version=1, title="test_title", code_sha256=q.digest(self.code))
        self.policy = dict(self.common, kind="policy", image_base="100000", min_pairs=300,
                           excluded=[dict(id="0000", name="Player")],
                           required_cases=["restore", "room_change", "destroy_reuse", "construction_over_budget"])
        proof = self.file("review.txt", b"synthetic semantic review; not game evidence")
        self.profile = dict(id="0021", name="TestActor", motion="stationary", draw="rigid",
                            all_variants_reviewed=True, variant_review="Synthetic complete parameter domain",
                            sources=[proof], guards=[dict(address="100000", word="04030201")],
                            required_cases=[], cases=[dict(case=k, passed=True, evidence=proof)
                                                     for k in self.policy["required_cases"]])
        self.session = dict(self.common, kind="session", id="synthetic", binary_sha256="1" * 64,
                            source_revision="2" * 40, provenance=proof, windows=[dict(begin_ms=50, end_ms=800)],
                            log=self.file("trace.log", (HEADER + record() + "\n").encode()))
        self.evidence = dict(self.common, kind="evidence", sessions=[self.session], profiles=[self.profile])

    def file(self, name, data):
        (self.root / name).write_bytes(data)
        return dict(path=name, sha256=q.digest(data))

    def trace(self, *lines):
        self.session["log"] = self.file("trace.log", (HEADER + "\n".join(lines) + "\n").encode())

    def run_report(self):
        return q.qualify(self.policy, self.evidence, self.root, self.code)

    def actor(self):
        return next(r for r in self.run_report()["profiles"] if r["id"] == "0x0021")

    def test_accepts_complete_synthetic_proof_and_excludes_player(self):
        report = self.run_report()
        self.assertEqual([r["decision"] for r in report["profiles"]], ["REJECT", "ACCEPT"])
        self.assertEqual(self.actor()["pairs"], 480)
        report.update(evidence_sha256="a" * 64, policy_sha256="b" * 64, qualifier_sha256="c" * 64)
        text = q.catalog_text(report)
        self.assertIn('TRIAEVUM_STATIC_SHADOW_ACTOR(0x0021, "TestActor")', text)
        self.assertNotIn("TRIAEVUM_STATIC_SHADOW_ACTOR(0x0000", text)

    def test_title_and_revision_cannot_cross_any_boundary(self):
        for target in (self.evidence, self.session):
            for key, value in (("title", "foreign"), ("code_sha256", "f" * 64)):
                original = target[key]
                target[key] = value
                with self.subTest(key=key), self.assertRaisesRegex(ValueError, "foreign"):
                    self.run_report()
                target[key] = original
        with self.assertRaisesRegex(ValueError, "code image"):
            q.qualify(self.policy, self.evidence, self.root, b"changed")

    def test_same_numeric_id_uses_only_selected_titles_exclusions(self):
        self.policy["excluded"].append(dict(id="0021", name="Reserved"))
        self.assertEqual(self.actor()["decision"], "REJECT")
        self.policy["excluded"].pop()
        self.assertEqual(self.actor()["decision"], "ACCEPT")

    def test_idle_discovery_is_not_all_variant_review(self):
        for key in ("all_variants_reviewed", "variant_review", "sources", "guards"):
            original = self.profile[key]
            self.profile[key] = [] if key in ("sources", "guards") else False
            with self.subTest(key=key):
                self.assertEqual(self.actor()["decision"], "INSUFFICIENT")
            self.profile[key] = original

    def test_source_and_log_hashes_are_verified(self):
        for filename in ("trace.log", "review.txt"):
            path = self.root / filename
            original = path.read_bytes()
            path.write_bytes(original + b"changed")
            with self.subTest(filename=filename), self.assertRaisesRegex(ValueError, "hash mismatch"):
                self.run_report()
            path.write_bytes(original)

    def test_native_guards_are_verified(self):
        for address, word in (("0ffffc", "04030201"), ("100001", "04030201"),
                              ("100004", "04030201"), ("100000", "04030200")):
            self.profile["guards"] = [dict(address=address, word=word)]
            with self.subTest(address=address, word=word), self.assertRaisesRegex(ValueError, "guard mismatch"):
                self.run_report()

    def test_missing_case_failed_case_and_nonboolean_are_not_passes(self):
        case = self.profile["cases"].pop()
        self.assertIn("required_cases_missing", self.actor()["reasons"])
        self.profile["cases"].append(case)
        case["passed"] = False
        self.assertEqual(self.actor()["decision"], "REJECT")
        case["passed"] = "false"
        with self.assertRaisesRegex(ValueError, "boolean"):
            self.run_report()

    def test_interactive_profiles_need_explicit_cases(self):
        for motion in sorted(q.CASE_MOTIONS):
            self.profile["motion"] = motion
            self.assertIn("interaction_manifest_missing", self.actor()["reasons"])
        self.profile["required_cases"] = ["cut_then_idle"]
        self.assertIn("required_cases_missing", self.actor()["reasons"])

    def test_continuous_temporary_or_skeletal_are_rejected(self):
        for motion, draw in (("continuous", "rigid"), ("temporary", "rigid"), ("stationary", "skeletal")):
            self.profile.update(motion=motion, draw=draw)
            self.assertEqual(self.actor()["decision"], "REJECT")

    def test_unknown_draw_and_unreviewed_actor_remain_dynamic(self):
        self.profile["draw"] = "unknown"
        self.assertIn("unproved_rigid_draw", self.actor()["reasons"])
        self.evidence["profiles"] = []
        self.assertIn("unreviewed_profile", self.actor()["reasons"])

    def test_no_observations_and_short_window_are_insufficient(self):
        self.trace()
        self.assertIn("not_observed", self.actor()["reasons"])
        self.trace(record(pairs=299, unchanged=299))
        self.assertIn("no_long_idle_window", self.actor()["reasons"])

    def test_lifetime_zero_does_not_qualify_stability(self):
        self.trace(record(lifetime=0))
        self.assertIn("missing_lifetime", self.actor()["reasons"])
        self.assertEqual(self.actor()["decision"], "INSUFFICIENT")

    def test_churn_in_any_instance_and_material_blip_are_not_forgiven(self):
        self.trace(record(), record(owner="0000002100002000", unchanged=479, version_changes=1,
                                   material_changes=1, first_change_frame=300, first_change_t=400))
        self.assertIn("idle_churn", self.actor()["reasons"])
        self.assertEqual(self.actor()["decision"], "REJECT")

    def test_cumulative_records_are_not_added_twice(self):
        self.trace(record(observed=401, pairs=280, unchanged=280, rigid=401, dynamic=401,
                          t_last=500, frame_last=401), record())
        self.assertEqual(self.actor()["segments"], 1)
        self.assertEqual(self.actor()["pairs"], 480)

    def test_expiry_and_reappearance_keep_both_segments(self):
        self.session["windows"][0]["end_ms"] = 2000
        self.trace(record(unchanged=479, version_changes=1, pose_changes=1,
                          first_change_frame=300, first_change_t=400),
                   record(t_first=900, t_last=1500, frame_first=801, frame_last=1401))
        self.assertEqual(self.actor()["segments"], 2)
        self.assertEqual(self.actor()["decision"], "REJECT")

    def test_cumulative_regression_and_lost_first_change_fail(self):
        bad = record(unchanged=479, version_changes=1, pose_changes=1,
                     first_change_frame=300, first_change_t=400)
        for final in (record(), record(unchanged=479, version_changes=1, pose_changes=1,
                                      first_change_frame=301, first_change_t=401)):
            self.trace(bad, final)
            with self.assertRaisesRegex(ValueError, "cumulative"):
                self.run_report()

    def test_record_crossing_visit_is_not_truncated_into_pass(self):
        self.session["windows"] = [dict(begin_ms=50, end_ms=650)]
        self.assertIn("outside_idle_window", self.actor()["reasons"])
        self.assertIn("no_long_idle_window", self.actor()["reasons"])

    def test_missing_windows_and_overlaps_fail_closed(self):
        self.session["windows"] = []
        self.assertEqual(self.actor()["decision"], "INSUFFICIENT")
        self.session["windows"] = [dict(begin_ms=50, end_ms=500), dict(begin_ms=500, end_ms=800)]
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.run_report()

    def test_nonrigid_caster_and_unbounded_construction_reject(self):
        for state in q.ZERO:
            self.trace(record(rigid=600, **{state: 1}))
            self.assertEqual(self.actor()["decision"], "REJECT")
        self.trace(record(), "[vsm-keyframe] v=1 target=1234 frame=600 t=700 event=unbounded age=500 pending=30")
        self.assertIn("unbounded_construction", self.actor()["reasons"])

    def test_world_geometry_never_becomes_player_evidence(self):
        self.trace(record(owner="0", actor="0000"))
        report = self.run_report()
        self.assertEqual(report["sessions"][0]["world_lines"], 1)
        self.assertEqual(sum(r["segments"] for r in report["profiles"]), 0)

    def test_malformed_or_future_records_cannot_be_silently_skipped(self):
        for line in (record(v=2), record(pairs=-1), record(unchanged=479), record(actor="0022"),
                     record(owner="0000002100000000"), record(parts_min=2), record(observed=0),
                     record(t_last=99), record(rigid=0), record(dynamic=602), record(version_changes=1),
                     record(material_changes=1), record() + " pairs=480"):
            self.trace(line)
            with self.subTest(line=line), self.assertRaises((ValueError, KeyError)):
                self.run_report()

    def test_multiple_sessions_with_same_actor_are_independent(self):
        other = copy.deepcopy(self.session)
        other["id"] = "second"
        self.evidence["sessions"].append(other)
        with self.assertRaisesRegex(ValueError, "duplicate log"):
            self.run_report()
        other["log"] = self.file("second.log", (HEADER + record(t_first=101, t_last=701)).encode())
        self.assertEqual(self.actor()["segments"], 2)
        self.assertEqual(self.actor()["pairs"], 960)

    def test_duplicate_inputs_and_concatenated_logs_refuse(self):
        for key in ("profiles", "sessions"):
            self.evidence[key].append(self.evidence[key][0])
            with self.assertRaisesRegex(ValueError, "duplicate"):
                self.run_report()
            self.evidence[key].pop()
        self.trace(record(), HEADER.strip(), record())
        with self.assertRaisesRegex(ValueError, "boundary"):
            self.run_report()
        path = self.root / "duplicate.json"
        path.write_text('{"passed":false,"passed":true}')
        with self.assertRaisesRegex(ValueError, "duplicate JSON"):
            q.load(path)

    def test_cli_refuses_overwrite_and_does_not_publish_on_invalid_input(self):
        for name, data in (("policy", self.policy), ("evidence", self.evidence)):
            (self.root / f"{name}.json").write_text(json.dumps(data))
        (self.root / "code.bin").write_bytes(self.code)
        out = self.root / "out"
        args = ["--policy", str(self.root / "policy.json"), "--evidence", str(self.root / "evidence.json"),
                "--code-bin", str(self.root / "code.bin"), "--out", str(out)]
        self.assertEqual(q.main(args), 0)
        before = (out / "qualification.json").read_bytes()
        with self.assertRaises(SystemExit) as error:
            q.main(args)
        self.assertEqual(error.exception.code, 1)
        self.assertEqual((out / "qualification.json").read_bytes(), before)
        (self.root / "trace.log").write_text("corrupt")
        args[-1] = str(self.root / "invalid")
        with self.assertRaises(SystemExit):
            q.main(args)
        self.assertFalse((self.root / "invalid").exists())

    def test_empty_discovery_is_not_a_publication_pass(self):
        self.evidence.update(sessions=[], profiles=[])
        for name, data in (("policy", self.policy), ("evidence", self.evidence)):
            (self.root / f"{name}.json").write_text(json.dumps(data))
        (self.root / "code.bin").write_bytes(self.code)
        self.assertEqual(q.main(["--policy", str(self.root / "policy.json"), "--evidence", str(self.root / "evidence.json"),
                                 "--code-bin", str(self.root / "code.bin"), "--out", str(self.root / "empty")]), 2)

    def test_deterministic_report(self):
        before = self.run_report()
        self.profile["cases"].reverse()
        self.policy["excluded"].reverse()
        self.assertEqual(before, self.run_report())


if __name__ == "__main__":
    unittest.main()
