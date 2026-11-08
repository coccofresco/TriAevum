"""Offline, title-bound qualification of v1 shadow-caster stability records.

Discovery is not eligibility. See README.md for the evidence contract. This tool
never launches a game, changes runtime policy, or treats missing proof as success.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

SCHEMA = "triaevum.static-shadow"
COUNTERS = ("observed", "pairs", "unchanged", "membership_changes", "version_changes",
            "rigid", "uncacheable", "skinned", "nonhomogeneous", "unavailable", "dynamic", "promoted")
FIELDS = COUNTERS + ("lifetime", "t_first", "t_last", "frame_first", "frame_last",
                     "first_change_frame", "first_change_t", "parts_min", "parts_max")
ZERO = ("uncacheable", "skinned", "nonhomogeneous", "unavailable")
DECOMPOSED = ("geometry_changes", "pose_changes", "material_changes")
MOTIONS = {"stationary", "interaction_only", "flag_state", "mixed", "continuous", "temporary"}
CASE_MOTIONS = {"interaction_only", "flag_state", "mixed"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    # Duplicate JSON keys must not silently replace a failed case or source id.
    def unique(pairs):
        out = {}
        for key, value in pairs:
            require(key not in out, f"duplicate JSON key: {key}")
            out[key] = value
        return out
    raw = Path(path).read_bytes()
    return json.loads(raw, object_pairs_hook=unique), digest(raw)


def natural(value, label):
    require(type(value) is int and value >= 0, f"invalid {label}")
    return value


def hex_value(value, bits, label):
    require(isinstance(value, str) and re.fullmatch(r"(?:0x)?[0-9a-fA-F]+", value), f"invalid {label}")
    result = int(value, 16)
    require(result < 1 << bits, f"out-of-range {label}")
    return result


def sha(value):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value), "invalid sha256")
    return value


def artifact(ref, root):
    require(isinstance(ref, dict) and isinstance(ref.get("path"), str) and ref["path"], "missing artifact path")
    path = Path(ref["path"])
    if not path.is_absolute():
        path = root / path
    data = path.read_bytes()
    require(digest(data) == sha(ref.get("sha256")), f"artifact hash mismatch: {path}")
    return data


def binding(document, kind, policy=None):
    require(document.get("schema") == SCHEMA and document.get("version") == 1 and document.get("kind") == kind,
            f"unsupported {kind} schema")
    title = document.get("title")
    require(isinstance(title, str) and re.fullmatch(r"[a-z][a-z0-9_-]{0,31}", title), "invalid title")
    sha(document.get("code_sha256"))
    if policy is not None:
        require(title == policy["title"] and document["code_sha256"] == policy["code_sha256"],
                f"foreign title/revision in {kind}")


def keyed(rows, key, parse=lambda v: v):
    require(isinstance(rows, list), "expected an array")
    result = {}
    for row in rows:
        require(isinstance(row, dict) and key in row, f"missing {key}")
        value = parse(row[key])
        require(value not in result, f"duplicate {key}: {value}")
        result[value] = row
    return result


def names(values):
    require(isinstance(values, list) and all(isinstance(v, str) and v.strip() for v in values), "invalid case manifest")
    require(len(values) == len(set(values)), "duplicate case name")
    return set(values)


def parse_trace(data):
    """Keep independent cumulative segments, including retire/reappear segments.

    Unlike the historical sweep aggregator, never estimate an idle subwindow
    from frame numbers, forgive material blips, or discard an older violation
    just because a later record for the same owner has more observations.
    """
    records, header, keyframes, worlds = {}, None, [], 0
    for number, line in enumerate(data.decode("utf-8", "strict").splitlines(), 1):
        tag = next((t for t in ("shadow-caster-trace", "shadow-caster-stability", "vsm-keyframe")
                    if f"[{t}]" in line), None)
        if tag is None:
            continue
        pairs = re.findall(r"(\w+)=(\S+)", line.split(f"[{tag}]", 1)[1])
        row = dict(pairs)
        require(len(row) == len(pairs) and row.get("v") == "1", f"invalid trace line {number}")
        if tag == "shadow-caster-trace":
            require(header is None and row.get("clock") == "unix_ms", "missing/duplicate trace session boundary")
            header = {k: int(row[k]) for k in ("warmup", "interval")}
            require(header["warmup"] >= 0 and header["interval"] > 0, "invalid trace cadence")
            continue
        require(header is not None, "record before trace header")
        if tag == "vsm-keyframe":
            require(row.get("event") == "unbounded", "unknown keyframe event")
            hex_value(row.get("target"), 64, "keyframe target")
            require(all(int(row[k]) >= 0 for k in ("frame", "t", "age", "pending")), "invalid keyframe counters")
            keyframes.append(row)
            continue
        record = {key: int(row[key]) for key in FIELDS}
        require(all(v >= 0 for v in record.values()), f"negative trace count at {number}")
        for key in ("target", "owner"):
            record[key] = hex_value(row.get(key), 64, key)
        record["actor"] = hex_value(row.get("actor"), 16, "actor")
        require(record["actor"] == (record["owner"] >> 32) and
                (record["owner"] == 0 or record["owner"] & 0xffffffff), "owner/profile mismatch")
        require(record["observed"] > 0 and 0 < record["parts_min"] <= record["parts_max"], "empty trace record")
        draws = record["rigid"] + sum(record[k] for k in ZERO)
        require(record["observed"] * record["parts_min"] <= draws <= record["observed"] * record["parts_max"] and
                record["dynamic"] <= draws and record["promoted"] <= draws, "inconsistent caster counts")
        require(record["pairs"] < record["observed"] and
                record["pairs"] == record["unchanged"] + record["membership_changes"] + record["version_changes"],
                "inconsistent pair counts")
        require(record["t_first"] <= record["t_last"] and record["frame_first"] <= record["frame_last"],
                "reversed trace interval")
        changed = record["membership_changes"] + record["version_changes"]
        require((changed == 0 and record["first_change_frame"] == record["first_change_t"] == 0) or
                (changed > 0 and record["frame_first"] <= record["first_change_frame"] <= record["frame_last"] and
                 record["t_first"] <= record["first_change_t"] <= record["t_last"]), "inconsistent first change")
        present = [key in row for key in DECOMPOSED]
        require(not any(present) or all(present), "partial version decomposition")
        if all(present):
            record.update({key: int(row[key]) for key in DECOMPOSED})
            require(all(record[k] >= 0 for k in DECOMPOSED) and
                    sum(record[k] for k in DECOMPOSED) == record["version_changes"], "invalid version decomposition")
        if record["owner"] == 0:
            worlds += 1
            continue
        # t_first/frame_first distinguish records recreated after tracer expiry.
        key = tuple(record[k] for k in ("target", "owner", "lifetime", "t_first", "frame_first"))
        previous = records.get(key)
        if previous is not None:
            require(all(record[k] >= previous[k] for k in COUNTERS + ("t_last", "frame_last", "parts_max")) and
                    record["parts_min"] <= previous["parts_min"], "cumulative counters regressed")
            if previous["membership_changes"] + previous["version_changes"]:
                require(all(record[k] == previous[k] for k in ("first_change_frame", "first_change_t")),
                        "cumulative first change replaced")
        records[key] = record
    require(header is not None, "no trace header")
    return list(records.values()), {"header": header, "world_lines": worlds, "unbounded": keyframes}


def qualify(policy, evidence, root, code):
    binding(policy, "policy")
    binding(evidence, "evidence", policy)
    require(digest(code) == policy["code_sha256"], "code image hash mismatch")
    base = hex_value(policy.get("image_base"), 32, "image base")
    minimum = natural(policy.get("min_pairs"), "min_pairs")
    require(minimum > 0, "min_pairs must be positive")
    global_cases = names(policy.get("required_cases"))
    excluded = keyed(policy.get("excluded"), "id", lambda v: hex_value(v, 16, "excluded id"))
    profiles = keyed(evidence.get("profiles"), "id", lambda v: hex_value(v, 16, "profile id"))
    sessions = keyed(evidence.get("sessions"), "id")
    records, session_reports, log_hashes = [], [], set()
    for session_id, session in sorted(sessions.items()):
        require(isinstance(session_id, str) and session_id, "invalid session id")
        binding(session, "session", policy)
        sha(session.get("binary_sha256"))
        # The run receipt is retained and content-bound, not independently
        # authenticated: source/binary declarations still require owner review.
        artifact(session["provenance"], root)
        require(isinstance(session.get("source_revision"), str) and
                re.fullmatch(r"[0-9a-f]{40,64}", session["source_revision"]), "missing source revision")
        windows = session.get("windows")
        require(isinstance(windows, list), "missing idle windows")
        intervals = []
        for window in windows:
            start, end = (natural(window[k], k) for k in ("begin_ms", "end_ms"))
            require(start < end, "empty idle window")
            intervals.append((start, end))
        intervals.sort()
        require(all(a[1] < b[0] for a, b in zip(intervals, intervals[1:])), "overlapping idle windows")
        rows, report = parse_trace(artifact(session["log"], root))
        log_sha = session["log"]["sha256"]
        require(log_sha not in log_hashes, "duplicate log in multiple sessions")
        log_hashes.add(log_sha)
        report.update(id=session_id, log_sha256=log_sha, records=len(rows),
                      binary_sha256=session["binary_sha256"], source_revision=session["source_revision"],
                      provenance_sha256=session["provenance"]["sha256"])
        session_reports.append(report)
        for row in rows:
            row.update(session=session_id, idle=any(a <= row["t_first"] <= row["t_last"] <= b for a, b in intervals))
            records.append(row)
    ids = sorted(set(profiles) | set(excluded) | {r["actor"] for r in records})
    results = []
    for pid in ids:
        profile, reasons, reject = profiles.get(pid), set(), False
        rows = [r for r in records if r["actor"] == pid]
        if pid in excluded:
            reasons.add("excluded_profile")
            reject = True
        if not rows:
            reasons.add("not_observed")
        if any(r["lifetime"] == 0 for r in rows):
            reasons.add("missing_lifetime")
        if any(not r["idle"] for r in rows):
            reasons.add("outside_idle_window")
        if any(r["idle"] and r["membership_changes"] + r["version_changes"] for r in rows):
            reasons.add("idle_churn")
            reject = True
        if any(sum(r[k] for k in ZERO) for r in rows):
            reasons.add("nonrigid_observation")
            reject = True
        if not any(r["idle"] and r["lifetime"] and r["pairs"] >= minimum and
                   r["unchanged"] == r["pairs"] and r["rigid"] > 0 for r in rows):
            reasons.add("no_long_idle_window")
        if any(report["unbounded"] for report in session_reports):
            reasons.add("unbounded_construction")
            reject = True
        if profile is None:
            reasons.add("unreviewed_profile")
        else:
            require(profile.get("motion") in MOTIONS and profile.get("draw") in ("rigid", "skeletal", "unknown"),
                    "invalid semantic classification")
            if profile["motion"] in ("continuous", "temporary") or profile["draw"] == "skeletal":
                reasons.add("semantic_reject")
                reject = True
            if profile["draw"] == "unknown":
                reasons.add("unproved_rigid_draw")
            review = profile.get("variant_review")
            if (profile.get("all_variants_reviewed") is not True or not isinstance(review, str) or not review.strip()):
                reasons.add("all_variant_review_missing")
            refs = profile.get("sources", [])
            require(isinstance(refs, list), "invalid sources")
            if not refs:
                reasons.add("semantic_sources_missing")
            for ref in refs:
                artifact(ref, root)
            guards = profile.get("guards", [])
            guards = list(keyed(guards, "address", lambda v: hex_value(v, 32, "guard address")).values())
            if not guards:
                reasons.add("native_guards_missing")
            for guard in guards:
                at = hex_value(guard["address"], 32, "guard address") - base
                word = hex_value(guard["word"], 32, "guard word")
                require(at >= 0 and at % 4 == 0 and at + 4 <= len(code) and
                        struct.unpack_from("<I", code, at)[0] == word, "native guard mismatch")
            required = names(profile.get("required_cases", []))
            if profile["motion"] in CASE_MOTIONS and not required:
                reasons.add("interaction_manifest_missing")
            required |= global_cases
            cases = keyed(profile.get("cases", []), "case")
            if required - cases.keys():
                reasons.add("required_cases_missing")
            for case in cases.values():
                require(type(case.get("passed")) is bool, "case passed must be boolean")
                artifact(case["evidence"], root)
                if not case["passed"]:
                    reasons.add("case_failed")
                    reject = True
        results.append({"id": f"0x{pid:04X}", "name": (profile or excluded.get(pid) or {}).get("name", "unknown"),
                        "decision": "REJECT" if reject else "INSUFFICIENT" if reasons else "ACCEPT",
                        "reasons": sorted(reasons), "segments": len(rows),
                        "pairs": sum(r["pairs"] for r in rows), "records": rows})
    return {"schema": SCHEMA, "version": 1, "kind": "qualification", "title": policy["title"],
            "code_sha256": policy["code_sha256"], "sessions": session_reports, "profiles": results,
            "scope": "reviewed profiles only; not whole-title or runtime qualification"}


def catalog_text(report):
    lines = ["// GENERATED; offline evidence only. Do not install without owning runtime gates.",
             f"// Title: {report['title']}; evidence SHA256: {report['evidence_sha256']}",
             f"// Policy SHA256: {report['policy_sha256']}; qualifier SHA256: {report['qualifier_sha256']}"]
    for row in report["profiles"]:
        if row["decision"] == "ACCEPT":
            require(isinstance(row["name"], str) and re.fullmatch(r"[A-Za-z0-9_]+", row["name"]), "invalid actor name")
            lines.append(f'TRIAEVUM_STATIC_SHADOW_ACTOR({row["id"]}, "{row["name"]}")')
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--code-bin", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="new directory (never overwrite a prior qualification)")
    args = parser.parse_args(argv)
    try:
        policy, policy_sha = load(args.policy)
        evidence, evidence_sha = load(args.evidence)
        report = qualify(policy, evidence, args.evidence.parent, args.code_bin.read_bytes())
        report.update(policy_sha256=policy_sha, evidence_sha256=evidence_sha,
                      qualifier_sha256=digest(Path(__file__).read_bytes()))
        text = catalog_text(report)
        args.out.mkdir(parents=True, exist_ok=False)
        (args.out / "qualification.json").write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        (args.out / f"{policy['title']}_static_shadow_actor_catalog.inc").write_text(text, encoding="utf-8")
        counts = {d: sum(p["decision"] == d for p in report["profiles"]) for d in ("ACCEPT", "INSUFFICIENT", "REJECT")}
        print(json.dumps(counts, sort_keys=True))
        # Empty or unresolved discovery is not a passed publication gate.
        return 0 if counts["ACCEPT"] and not counts["INSUFFICIENT"] else 2
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f"static-shadow qualification refused: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
