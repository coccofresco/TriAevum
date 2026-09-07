from __future__ import annotations

from collections import Counter
import csv
import json
import math
from pathlib import Path
from statistics import mean

from .anb_export import ANB_FRAME_CHANNEL_COUNT_CANDIDATE
from .binary import ParseError
from .character_conversion_package import sample_csab_track_channel
from .romfs_inventory import sorted_counter


ANB_SEMANTIC_AUDIT_FORMAT = "oot3d_anb_semantic_candidate_audit_v1"


def audit_anb_semantic_candidates(
    anb_export_path: Path,
    character_manifest_path: Path,
    output_path: Path,
    *,
    track_root: Path | None = None,
    min_abs_correlation: float = 0.98,
    min_consensus_count: int = 2,
    sample_limit: int = 20,
    unresolved_channel_csv_output_path: Path | None = None,
    include_frame_mismatch_resample: bool = False,
) -> dict[str, object]:
    anb_export = read_json(anb_export_path)
    character_manifest = read_json(character_manifest_path)
    if anb_export.get("format") != "oot3d_anb_payload_batch_export_v1":
        raise ParseError(f"{anb_export_path}: unexpected ANB export format")
    if character_manifest.get("format") != "oot3d_character_conversion_manifest_v1":
        raise ParseError(f"{character_manifest_path}: unexpected character manifest format")

    if track_root is None:
        track_root = character_manifest_path.parent.parent / "skinned_animation_batch" / "tracks"

    track_entries = csab_track_entries(character_manifest)
    anb_records = [
        record for record in anb_export.get("records", [])
        if isinstance(record, dict) and record.get("csab_lookup", {}).get("status") == "matched"
    ]

    compared_records: list[dict[str, object]] = []
    missing_track_records: list[dict[str, object]] = []
    channel_candidates: dict[int, list[dict[str, object]]] = {index: [] for index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)}
    status_counts: Counter[str] = Counter()
    component_counts: Counter[str] = Counter()
    total_pairs = 0

    for anb_record in anb_records:
        lookup = anb_record.get("csab_lookup", {})
        if not isinstance(lookup, dict):
            continue
        csab_name = str(lookup.get("csab_name") or "")
        frame_count = int(anb_record.get("metadata", {}).get("decoded_frame_count", 0) or 0)
        csab_frame_slots = int(lookup.get("frame_slot_count", 0) or 0)
        if frame_count <= 0 or csab_frame_slots <= 0:
            status_counts["skipped_frame_count_mismatch"] += 1
            continue
        comparison_kind = "direct_frame_aligned"
        if frame_count != csab_frame_slots:
            if not include_frame_mismatch_resample:
                status_counts["skipped_frame_count_mismatch"] += 1
                continue
            comparison_kind = "resampled_frame_count_mismatch"
        track_entry = track_entries.get(normalize_path(csab_name))
        if not track_entry:
            status_counts["missing_track_entry"] += 1
            append_sample(missing_track_records, sample_limit, {"csab_name": csab_name, "reason": "missing_track_entry"})
            continue
        track_path = resolve_track_path(track_root, str(track_entry))
        if not track_path.is_file():
            status_counts["missing_track_file"] += 1
            append_sample(
                missing_track_records,
                sample_limit,
                {"csab_name": csab_name, "track_entry": track_entry, "path": str(track_path)},
            )
            continue

        csab_track = read_json(track_path)
        anb_frames = decoded_anb_frames(anb_record)
        components = sampled_csab_components(
            csab_track,
            frame_count,
            source_frame_slots=csab_frame_slots,
        )
        record_best = best_record_candidates(anb_frames, components, min_abs_correlation)
        for candidate in record_best:
            candidate["comparison_kind"] = comparison_kind
            candidate["embedded_name"] = anb_record.get("embedded_name")
            candidate["csab_name"] = csab_name
            candidate["anb_frame_count"] = frame_count
            candidate["csab_frame_slot_count"] = csab_frame_slots
            channel_candidates[int(candidate["anb_channel"])].append(candidate)
            component_counts[str(candidate["component"])] += 1
        total_pairs += len(anb_frames) * len(components)
        status_counts["compared"] += 1
        status_counts[comparison_kind] += 1
        append_sample(
            compared_records,
            sample_limit,
            {
                "embedded_name": anb_record.get("embedded_name"),
                "csab_name": csab_name,
                "frame_count": frame_count,
                "csab_frame_slot_count": csab_frame_slots,
                "comparison_kind": comparison_kind,
                "component_count": len(components),
                "candidate_count": len(record_best),
                "sample_candidates": record_best[:5],
            },
        )

    channel_summary = summarize_channel_candidates(channel_candidates)
    stable_mappings = stable_channel_mappings(channel_candidates, min_consensus_count)
    candidate_channel_count = sum(1 for record in channel_summary if record["candidate_count"] > 0)
    strong_candidate_count = sum(record["candidate_count"] for record in channel_summary)
    stable_channel_count = len(stable_mappings)
    zero_candidate_channel_count = sum(1 for record in channel_summary if record["candidate_count"] == 0)
    unstable_candidate_channel_count = candidate_channel_count - stable_channel_count
    export_channel_summary = (
        anb_export.get("channel_summary")
        if isinstance(anb_export.get("channel_summary"), dict)
        else {}
    )
    batch_profiles = batch_anb_channel_profiles(anb_export)
    zero_candidate_classification = zero_candidate_channel_classification(
        channel_summary,
        export_channel_summary,
        batch_profiles,
    )
    zero_candidate_resolution_counts = zero_candidate_classification["resolution_counts"]
    resolved_zero_candidate_channel_count = sum(
        int(count)
        for status, count in zero_candidate_resolution_counts.items()
        if str(status).startswith("resolved_")
    )
    resolved_static_zero_candidate_channel_count = int(
        zero_candidate_resolution_counts.get("resolved_static_zero_padding_or_reserved", 0)
    )
    resolved_constant_control_tuple_channel_count = int(
        zero_candidate_resolution_counts.get("resolved_constant_per_record_control_tuple", 0)
    )
    unresolved_zero_candidate_channel_count = zero_candidate_channel_count - resolved_zero_candidate_channel_count
    unresolved_channel_count = (
        ANB_FRAME_CHANNEL_COUNT_CANDIDATE
        - stable_channel_count
        - resolved_zero_candidate_channel_count
    )
    channel_resolution_counts = {
        "stable_candidate_mapping": stable_channel_count,
        "candidate_but_not_stable": unstable_candidate_channel_count,
        "no_candidate": zero_candidate_channel_count,
        "resolved_zero_candidate_non_transform": resolved_zero_candidate_channel_count,
        "resolved_zero_static_non_transform": resolved_static_zero_candidate_channel_count,
        "resolved_constant_control_tuple_non_transform": resolved_constant_control_tuple_channel_count,
        "remaining_unresolved": unresolved_channel_count,
    }
    unstable_candidate_classification = unstable_candidate_channel_classification(
        channel_summary,
        stable_mappings,
        min_consensus_count,
    )
    unresolved_channel_worklist = build_unresolved_channel_worklist(
        channel_summary,
        stable_mappings,
        zero_candidate_classification["records"],
        unstable_candidate_classification["records"],
    )
    audit = {
        "format": ANB_SEMANTIC_AUDIT_FORMAT,
        "anb_export": str(anb_export_path),
        "character_manifest": str(character_manifest_path),
        "track_root": str(track_root),
        "matched_anb_record_count": len(anb_records),
        "compared_record_count": status_counts["compared"],
        "status_counts": sorted_counter(status_counts),
        "total_compared_frame_component_pairs": total_pairs,
        "min_abs_correlation": min_abs_correlation,
        "min_consensus_count": min_consensus_count,
        "include_frame_mismatch_resample": include_frame_mismatch_resample,
        "channel_count": ANB_FRAME_CHANNEL_COUNT_CANDIDATE,
        "candidate_channel_count": candidate_channel_count,
        "strong_candidate_count": strong_candidate_count,
        "stable_candidate_channel_count": stable_channel_count,
        "resolved_zero_candidate_channel_count": resolved_zero_candidate_channel_count,
        "resolved_static_zero_candidate_channel_count": resolved_static_zero_candidate_channel_count,
        "resolved_constant_control_tuple_channel_count": resolved_constant_control_tuple_channel_count,
        "zero_candidate_channel_count": zero_candidate_channel_count,
        "unresolved_zero_candidate_channel_count": unresolved_zero_candidate_channel_count,
        "unstable_candidate_channel_count": unstable_candidate_channel_count,
        "unresolved_channel_count": unresolved_channel_count,
        "channel_resolution_counts": channel_resolution_counts,
        "zero_candidate_channel_class_counts": zero_candidate_classification["counts"],
        "unresolved_zero_candidate_channel_class_counts": zero_candidate_classification["unresolved_counts"],
        "zero_candidate_semantic_class_counts": zero_candidate_classification["semantic_counts"],
        "zero_candidate_resolution_status_counts": zero_candidate_classification["resolution_counts"],
        "zero_candidate_channel_classification": zero_candidate_classification["records"],
        "unstable_candidate_channel_class_counts": unstable_candidate_classification["counts"],
        "unstable_candidate_semantic_class_counts": unstable_candidate_classification["semantic_counts"],
        "unstable_candidate_channel_classification": unstable_candidate_classification["records"],
        "unresolved_channel_worklist_count": len(unresolved_channel_worklist),
        "unresolved_channel_worklist": unresolved_channel_worklist,
        "semantic_status": {
            "channel_contract_status": (
                "complete"
                if stable_channel_count == ANB_FRAME_CHANNEL_COUNT_CANDIDATE
                else "partial"
            ),
            "promotion_status": (
                "promotion_ready"
                if stable_channel_count == ANB_FRAME_CHANNEL_COUNT_CANDIDATE
                else "blocked"
            ),
            "blocking_issues": anb_blocking_issues(
                compared_record_count=status_counts["compared"],
                unstable_candidate_channel_count=unstable_candidate_channel_count,
                unresolved_zero_candidate_channel_count=zero_candidate_channel_count - resolved_zero_candidate_channel_count,
            ),
        },
        "resolution_gates": anb_resolution_gates(
            unstable_candidate_channel_count=unstable_candidate_channel_count,
            zero_candidate_channel_count=unresolved_zero_candidate_channel_count,
            zero_candidate_class_counts=zero_candidate_classification["unresolved_counts"],
            unstable_candidate_class_counts=unstable_candidate_classification["counts"],
        ),
        "stable_candidate_mappings": stable_mappings,
        "component_candidate_counts": sorted_counter(component_counts),
        "channel_candidates": channel_summary,
        "sample_compared_records": compared_records,
        "sample_missing_track_records": missing_track_records,
        "contract": (
            "This is a semantic-candidate audit, not a final ANB player contract. It correlates decoded ANB "
            "s16 frame channels against CSAB track components for matched ANB/CSAB records, with frame-count "
            "mismatch resampling reported separately when enabled, so channel naming can be advanced with "
            "explicit evidence."
        ),
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(audit, indent=2), encoding="utf-8", newline="\n")
    if unresolved_channel_csv_output_path is not None:
        write_unresolved_channel_csv(unresolved_channel_csv_output_path, unresolved_channel_worklist)
    return audit


def csab_track_entries(character_manifest: dict[str, object]) -> dict[str, str]:
    entries: dict[str, str] = {}
    target = character_manifest.get("target", {})
    target_archive = normalize_path(str(target.get("archive_path") or "")) if isinstance(target, dict) else ""
    target_cmb = normalize_path(str(target.get("target_cmb_name") or "")) if isinstance(target, dict) else ""
    source_manifests = character_manifest.get("source_manifests", {})
    binding_manifest_path = (
        Path(str(source_manifests.get("skinned_animation_binding")))
        if isinstance(source_manifests, dict) and source_manifests.get("skinned_animation_binding")
        else None
    )
    if binding_manifest_path is not None and binding_manifest_path.is_file():
        binding_manifest = read_json(binding_manifest_path)
        for binding_target in binding_manifest.get("targets", []):
            if not isinstance(binding_target, dict):
                continue
            archive_path = normalize_path(str(binding_target.get("archive_path") or ""))
            cmb_name = normalize_path(str(binding_target.get("target_cmb_name") or ""))
            if target_archive and archive_path != target_archive:
                continue
            if target_cmb and cmb_name != target_cmb:
                continue
            for animation in binding_target.get("animations", []):
                if not isinstance(animation, dict):
                    continue
                csab_name = normalize_path(str(animation.get("csab_name") or ""))
                entry = str(
                    animation.get("track_export_file")
                    or animation.get("track_export")
                    or animation.get("track_package_entry")
                    or ""
                )
                if csab_name and entry:
                    entries[csab_name] = entry

    tracks = character_manifest.get("csab_animation_tracks", {})
    samples = tracks.get("sample_tracks", []) if isinstance(tracks, dict) else []
    if isinstance(samples, list):
        for record in samples:
            if not isinstance(record, dict):
                continue
            csab_name = normalize_path(str(record.get("csab_name") or ""))
            entry = str(record.get("track_package_entry") or "")
            if csab_name and entry:
                entries[csab_name] = entry
    # sample_tracks is currently complete for Link child, but keep a fallback for future manifest shapes.
    for record in character_manifest.get("animations", []) if isinstance(character_manifest.get("animations"), list) else []:
        if not isinstance(record, dict):
            continue
        csab_name = normalize_path(str(record.get("csab_name") or ""))
        entry = str(record.get("track_package_entry") or record.get("resource_path") or "")
        if csab_name and entry:
            entries[csab_name] = entry
    return entries


def resolve_track_path(track_root: Path, track_entry: str) -> Path:
    direct = Path(track_entry)
    if direct.is_file():
        return direct
    name = Path(track_entry.replace("\\", "/")).name
    return track_root / name


def decoded_anb_frames(record: dict[str, object]) -> list[list[int]]:
    metadata = record.get("metadata", {})
    frame_count = int(metadata.get("decoded_frame_count", 0) or 0) if isinstance(metadata, dict) else 0
    payload_words = record.get("payload_words", [])
    if frame_count <= 0 or not isinstance(payload_words, list):
        return []
    samples: list[int] = []
    for word in payload_words:
        value = int(word)
        low = value & 0xFFFF
        high = (value >> 16) & 0xFFFF
        samples.append(signed16(low))
        samples.append(signed16(high))
    trailing_bytes = bytes.fromhex(str(record.get("trailing_bytes_hex") or ""))
    if len(trailing_bytes) >= 2:
        samples.append(signed16(int.from_bytes(trailing_bytes[:2], "little")))
    expected = frame_count * ANB_FRAME_CHANNEL_COUNT_CANDIDATE
    samples = samples[:expected]
    if len(samples) != expected:
        return []
    return [
        samples[index * ANB_FRAME_CHANNEL_COUNT_CANDIDATE: (index + 1) * ANB_FRAME_CHANNEL_COUNT_CANDIDATE]
        for index in range(frame_count)
    ]


def sampled_csab_components(
    track_json: dict[str, object],
    output_frame_count: int,
    *,
    source_frame_slots: int | None = None,
) -> dict[str, list[float]]:
    components: dict[str, list[float]] = {}
    source_frame_slots = output_frame_count if source_frame_slots is None else source_frame_slots
    sample_frames = proportional_sample_frames(output_frame_count, source_frame_slots)
    for track in track_json.get("tracks", []):
        if not isinstance(track, dict):
            continue
        bone_index = int(track.get("bone_index", -1) or -1)
        for channel in track.get("channels", []):
            if not isinstance(channel, dict):
                continue
            semantic = normalize_path(str(channel.get("semantic") or f"slot_{channel.get('slot')}"))
            component = f"bone_{bone_index}:{semantic}"
            values = [sample_csab_track_channel(channel, frame) for frame in sample_frames]
            if all(math.isfinite(value) for value in values) and len(set(round(value, 6) for value in values)) > 1:
                components[component] = values
    return components


def proportional_sample_frames(output_frame_count: int, source_frame_slots: int) -> list[float]:
    output_frame_count = max(0, int(output_frame_count))
    source_frame_slots = max(0, int(source_frame_slots))
    if output_frame_count <= 0:
        return []
    if output_frame_count == source_frame_slots:
        return [float(frame) for frame in range(output_frame_count)]
    if output_frame_count == 1 or source_frame_slots <= 1:
        return [0.0 for _ in range(output_frame_count)]
    scale = float(source_frame_slots - 1) / float(output_frame_count - 1)
    return [float(frame) * scale for frame in range(output_frame_count)]


def best_record_candidates(
    anb_frames: list[list[int]],
    components: dict[str, list[float]],
    min_abs_correlation: float,
) -> list[dict[str, object]]:
    if not anb_frames or not components:
        return []
    frame_count = len(anb_frames)
    candidates: list[dict[str, object]] = []
    for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
        x = [float(frame[channel_index]) for frame in anb_frames]
        if len(set(x)) <= 1:
            continue
        best: dict[str, object] | None = None
        for component, y in components.items():
            if len(y) != frame_count or len(set(round(value, 6) for value in y)) <= 1:
                continue
            fit = linear_fit(x, y)
            if fit is None or abs(float(fit["correlation"])) < min_abs_correlation:
                continue
            record = {
                "anb_channel": channel_index,
                "component": component,
                **fit,
            }
            if best is None or abs(float(record["correlation"])) > abs(float(best["correlation"])):
                best = record
        if best is not None:
            candidates.append(best)
    return candidates


def summarize_channel_candidates(channel_candidates: dict[int, list[dict[str, object]]]) -> list[dict[str, object]]:
    summary: list[dict[str, object]] = []
    for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
        records = channel_candidates[channel_index]
        component_counts = Counter(str(record["component"]) for record in records)
        correlations = [float(record["correlation"]) for record in records]
        abs_correlations = [abs(value) for value in correlations]
        best = max(records, key=lambda record: abs(float(record["correlation"]))) if records else None
        summary.append(
            {
                "anb_channel": channel_index,
                "candidate_count": len(records),
                "component_counts": sorted_counter(component_counts),
                "component_evidence": component_evidence_summary(records),
                "mean_abs_correlation": round_float(mean(abs_correlations)) if abs_correlations else None,
                "best_candidate": round_candidate(best) if best is not None else None,
            }
        )
    return summary


def component_evidence_summary(records: list[dict[str, object]], *, sample_limit: int = 6) -> list[dict[str, object]]:
    by_component: dict[str, list[dict[str, object]]] = {}
    for record in records:
        component = str(record.get("component") or "")
        if not component:
            continue
        by_component.setdefault(component, []).append(record)
    summary: list[dict[str, object]] = []
    for component, component_records in by_component.items():
        abs_correlations = [abs(float(record["correlation"])) for record in component_records]
        signed_correlations = [float(record["correlation"]) for record in component_records]
        slopes = [float(record["slope"]) for record in component_records]
        intercepts = [float(record["intercept"]) for record in component_records]
        max_errors = [float(record["max_abs_error"]) for record in component_records]
        direct_records = [
            record for record in component_records
            if record.get("comparison_kind") == "direct_frame_aligned"
        ]
        resampled_records = [
            record for record in component_records
            if record.get("comparison_kind") == "resampled_frame_count_mismatch"
        ]
        direct_max_errors = [float(record["max_abs_error"]) for record in direct_records]
        resampled_max_errors = [float(record["max_abs_error"]) for record in resampled_records]
        evidence_records = sorted(
            component_records,
            key=lambda record: (
                str(record.get("embedded_name") or ""),
                str(record.get("csab_name") or ""),
            ),
        )
        summary.append(
            {
                "component": component,
                "count": len(component_records),
                "mean_abs_correlation": round_float(mean(abs_correlations)),
                "mean_signed_correlation": round_float(mean(signed_correlations)),
                "mean_slope": round_float(mean(slopes)),
                "mean_intercept": round_float(mean(intercepts)),
                "max_abs_error": round_float(max(max_errors)),
                "direct_record_count": len(direct_records),
                "resampled_record_count": len(resampled_records),
                "direct_max_abs_error": round_float(max(direct_max_errors)) if direct_max_errors else None,
                "resampled_max_abs_error": round_float(max(resampled_max_errors)) if resampled_max_errors else None,
                "sample_records": [
                    {
                        "embedded_name": record.get("embedded_name"),
                        "csab_name": record.get("csab_name"),
                        "comparison_kind": record.get("comparison_kind"),
                        "correlation": round_float(float(record.get("correlation", 0.0))),
                        "slope": round_float(float(record.get("slope", 0.0))),
                        "intercept": round_float(float(record.get("intercept", 0.0))),
                        "max_abs_error": round_float(float(record.get("max_abs_error", 0.0))),
                        "sample_count": record.get("sample_count"),
                    }
                    for record in evidence_records[:sample_limit]
                ],
            }
        )
    return sorted(summary, key=lambda item: (-int(item["count"]), str(item["component"])))


def stable_channel_mappings(
    channel_candidates: dict[int, list[dict[str, object]]],
    min_consensus_count: int,
) -> list[dict[str, object]]:
    stable: list[dict[str, object]] = []
    min_consensus_count = max(1, int(min_consensus_count))
    for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
        records = channel_candidates[channel_index]
        if not records:
            continue
        component_counts = Counter(str(record["component"]) for record in records)
        ranked_components = sorted(component_counts.items(), key=lambda item: (-item[1], item[0]))
        component, count = ranked_components[0]
        if count < min_consensus_count:
            continue
        if len(ranked_components) > 1 and ranked_components[1][1] == count:
            continue
        component_records = [record for record in records if str(record["component"]) == component]
        abs_correlations = [abs(float(record["correlation"])) for record in component_records]
        signed_correlations = [float(record["correlation"]) for record in component_records]
        slopes = [float(record["slope"]) for record in component_records]
        intercepts = [float(record["intercept"]) for record in component_records]
        max_errors = [float(record["max_abs_error"]) for record in component_records]
        stable.append(
            {
                "anb_channel": channel_index,
                "component": component,
                "consensus_count": count,
                "total_candidate_count": len(records),
                "consensus_share": round_float(count / len(records)),
                "mean_abs_correlation": round_float(mean(abs_correlations)),
                "mean_signed_correlation": round_float(mean(signed_correlations)),
                "mean_slope": round_float(mean(slopes)),
                "mean_intercept": round_float(mean(intercepts)),
                "max_abs_error": round_float(max(max_errors)),
            }
        )
    return stable


def anb_blocking_issues(
    *,
    compared_record_count: int,
    unstable_candidate_channel_count: int,
    unresolved_zero_candidate_channel_count: int,
) -> list[str]:
    issues: list[str] = []
    if compared_record_count <= 0:
        issues.append("anb-csab-no-compared-records")
    if unstable_candidate_channel_count > 0:
        issues.append("anb-channels-have-candidates-but-no-stable-consensus")
    if unresolved_zero_candidate_channel_count > 0:
        issues.append("anb-channels-without-csab-component-candidates")
    return issues


def anb_resolution_gates(
    *,
    unstable_candidate_channel_count: int,
    zero_candidate_channel_count: int,
    zero_candidate_class_counts: dict[str, int] | None = None,
    unstable_candidate_class_counts: dict[str, int] | None = None,
) -> list[dict[str, object]]:
    gates: list[dict[str, object]] = []
    if unstable_candidate_channel_count:
        class_counts = unstable_candidate_class_counts or {}
        required_evidence = "additional matched ANB/CSAB records, stronger consensus, or channel-family constraints"
        if class_counts:
            required_evidence = (
                "class-specific handling: resolve tied top components, gather more evidence for low-count "
                "candidates, and constrain diffuse single-hit channels by channel family or pose"
            )
        gates.append(
            {
                "gate": "stabilize_candidate_anb_channels",
                "record_count": unstable_candidate_channel_count,
                "class_counts": class_counts,
                "required_evidence": required_evidence,
            }
        )
    if zero_candidate_channel_count:
        class_counts = zero_candidate_class_counts or {}
        required_evidence = "additional CSAB components, constant-channel handling, or proof that the channel is padding/unused"
        if class_counts:
            required_evidence = (
                "class-specific handling: prove always-zero channels are padding/unused, "
                "identify low-cardinality control/event channels, and map remaining varying channels"
            )
        gates.append(
            {
                "gate": "identify_zero_candidate_anb_channels",
                "record_count": zero_candidate_channel_count,
                "class_counts": class_counts,
                "required_evidence": required_evidence,
            }
        )
    return gates


def unstable_candidate_channel_classification(
    channel_summary: list[dict[str, object]],
    stable_mappings: list[dict[str, object]],
    min_consensus_count: int,
) -> dict[str, object]:
    stable_channels = {int(record["anb_channel"]) for record in stable_mappings}
    min_consensus_count = max(1, int(min_consensus_count))
    records: list[dict[str, object]] = []
    counts: Counter[str] = Counter()
    semantic_counts: Counter[str] = Counter()
    for record in channel_summary:
        channel_index = int(record.get("anb_channel", -1))
        candidate_count = int(record.get("candidate_count", 0) or 0)
        if candidate_count <= 0 or channel_index in stable_channels:
            continue
        component_counts = record.get("component_counts") if isinstance(record.get("component_counts"), dict) else {}
        ranked_components = sorted(
            ((str(component), int(count or 0)) for component, count in component_counts.items()),
            key=lambda item: (-item[1], item[0]),
        )
        top_count = ranked_components[0][1] if ranked_components else 0
        second_count = ranked_components[1][1] if len(ranked_components) > 1 else 0
        kind = unstable_candidate_channel_kind(
            candidate_count,
            top_count,
            second_count,
            min_consensus_count,
        )
        semantic_class = unstable_candidate_semantic_class(kind)
        counts[kind] += 1
        semantic_counts[semantic_class] += 1
        records.append(
            {
                "anb_channel": channel_index,
                "kind": kind,
                "semantic_class": semantic_class,
                "candidate_count": candidate_count,
                "top_component": ranked_components[0][0] if ranked_components else None,
                "top_component_count": top_count,
                "second_component_count": second_count,
                "top_component_share": round_float(top_count / candidate_count) if candidate_count else None,
                "component_counts": component_counts,
                "mean_abs_correlation": record.get("mean_abs_correlation"),
                "best_candidate": record.get("best_candidate"),
                "required_evidence": unstable_candidate_channel_required_evidence(kind),
            }
        )
    return {
        "counts": sorted_counter(counts),
        "semantic_counts": sorted_counter(semantic_counts),
        "records": records,
    }


def unstable_candidate_channel_kind(
    candidate_count: int,
    top_count: int,
    second_count: int,
    min_consensus_count: int,
) -> str:
    if candidate_count == 1:
        return "single_record_candidate"
    if top_count >= min_consensus_count and second_count == top_count:
        return "tied_top_component_consensus"
    if top_count < min_consensus_count and candidate_count <= 2:
        return "low_evidence_component_pair"
    if top_count < min_consensus_count:
        return "diffuse_single_hit_components"
    return "unclassified_unstable_candidate"


def unstable_candidate_semantic_class(kind: str) -> str:
    if kind == "tied_top_component_consensus":
        return "component_collision_requires_disambiguation"
    if kind in {"single_record_candidate", "low_evidence_component_pair"}:
        return "insufficient_record_support"
    if kind == "diffuse_single_hit_components":
        return "diffuse_component_family_requires_constraints"
    return "unclassified_unstable_candidate"


def unstable_candidate_channel_required_evidence(kind: str) -> str:
    if kind == "tied_top_component_consensus":
        return "break the tied component consensus with more matched records, pose evidence, or channel-family constraints"
    if kind == "single_record_candidate":
        return "find at least one independent matched record for the same component before treating the channel as stable"
    if kind == "low_evidence_component_pair":
        return "add matched records or family constraints; current evidence is only one hit per competing component"
    if kind == "diffuse_single_hit_components":
        return "derive channel-family constraints or pose evidence because many components have only one supporting record"
    return "classify the unstable candidate pattern before promotion"


def zero_candidate_channel_classification(
    channel_summary: list[dict[str, object]],
    export_channel_summary: dict[str, object],
    batch_profiles: dict[int, dict[str, object]],
) -> dict[str, object]:
    bounds_by_channel: dict[int, dict[str, object]] = {}
    for bound in export_channel_summary.get("channel_bounds", []):
        if not isinstance(bound, dict):
            continue
        bounds_by_channel[int(bound.get("channel_index", -1) or -1)] = bound

    records: list[dict[str, object]] = []
    counts: Counter[str] = Counter()
    semantic_counts: Counter[str] = Counter()
    resolution_counts: Counter[str] = Counter()
    unresolved_counts: Counter[str] = Counter()
    for record in channel_summary:
        if int(record.get("candidate_count", 0) or 0) != 0:
            continue
        channel_index = int(record.get("anb_channel", -1))
        bounds = bounds_by_channel.get(channel_index, {})
        profile = batch_profiles.get(channel_index, {})
        distinct_count = int(bounds.get("distinct_sample_count", 0) or 0)
        nonzero_count = int(bounds.get("nonzero_sample_count", 0) or 0)
        kind = zero_candidate_channel_kind(distinct_count, nonzero_count)
        semantic_class = zero_candidate_semantic_class(kind, bounds, profile)
        resolution_status = zero_candidate_resolution_status(kind, semantic_class, bounds, profile)
        counts[kind] += 1
        semantic_counts[semantic_class] += 1
        resolution_counts[resolution_status] += 1
        if not resolution_status.startswith("resolved_"):
            unresolved_counts[kind] += 1
        records.append(
            {
                "anb_channel": channel_index,
                "kind": kind,
                "semantic_class": semantic_class,
                "resolution_status": resolution_status,
                "min": bounds.get("min"),
                "max": bounds.get("max"),
                "distinct_sample_count": distinct_count,
                "nonzero_sample_count": nonzero_count,
                "active_record_count": profile.get("active_record_count", 0),
                "active_constant_record_count": profile.get("active_constant_record_count", 0),
                "per_record_varying_record_count": profile.get("per_record_varying_record_count", 0),
                "active_constant_values": profile.get("active_constant_values", []),
                "per_record_varying_record_sample": profile.get("per_record_varying_record_sample", []),
                "top_values": profile.get("top_values", []),
                "active_record_sample": profile.get("active_record_sample", []),
                "required_evidence": zero_candidate_channel_required_evidence(kind, resolution_status),
            }
        )
    return {
        "counts": sorted_counter(counts),
        "semantic_counts": sorted_counter(semantic_counts),
        "resolution_counts": sorted_counter(resolution_counts),
        "unresolved_counts": sorted_counter(unresolved_counts),
        "records": records,
    }


def zero_candidate_channel_kind(distinct_count: int, nonzero_count: int) -> str:
    if nonzero_count == 0:
        return "always_zero_static"
    if distinct_count <= 2:
        return "low_cardinality_control_or_event"
    return "varying_unmatched_signal"


def zero_candidate_channel_required_evidence(kind: str, resolution_status: str | None = None) -> str:
    if resolution_status == "resolved_constant_per_record_control_tuple":
        return "preserve as non-transform per-record control tuple; do not map to CSAB transform"
    if kind == "always_zero_static":
        return "prove padding, unused slot, or implicit zero transform before treating as resolved"
    if kind == "low_cardinality_control_or_event":
        return "identify whether the channel is an event/control field or a quantized transform component"
    return "find a CSAB component, derived transform, or non-CSAB semantic source"


def zero_candidate_resolution_status(
    kind: str,
    semantic_class: str,
    bounds: dict[str, object],
    profile: dict[str, object],
) -> str:
    nonzero_count = int(bounds.get("nonzero_sample_count", 0) or 0)
    active_record_count = int(profile.get("active_record_count", 0) or 0)
    active_constant_record_count = int(profile.get("active_constant_record_count", 0) or 0)
    per_record_varying_record_count = int(profile.get("per_record_varying_record_count", 0) or 0)
    if (
        kind == "always_zero_static"
        and semantic_class == "static_zero_padding_or_reserved"
        and nonzero_count == 0
        and active_record_count == 0
    ):
        return "resolved_static_zero_padding_or_reserved"
    if (
        kind == "low_cardinality_control_or_event"
        and semantic_class == "constant_per_record_control_tuple"
        and nonzero_count > 0
        and active_record_count > 0
        and per_record_varying_record_count == 0
        and active_constant_record_count == active_record_count
    ):
        return "resolved_constant_per_record_control_tuple"
    return "unresolved_requires_semantic_source"


def batch_anb_channel_profiles(anb_export: dict[str, object]) -> dict[int, dict[str, object]]:
    counters: dict[int, Counter[int]] = {
        index: Counter() for index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)
    }
    active_records: dict[int, list[str]] = {
        index: [] for index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)
    }
    active_constant_records: dict[int, list[str]] = {
        index: [] for index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)
    }
    active_constant_values: dict[int, Counter[int]] = {
        index: Counter() for index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)
    }
    per_record_varying_records: dict[int, list[dict[str, object]]] = {
        index: [] for index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)
    }
    frame_counts: Counter[int] = Counter()
    for record in anb_export.get("records", []):
        if not isinstance(record, dict):
            continue
        frames = decoded_anb_frames(record)
        if not frames:
            continue
        frame_counts[len(frames)] += 1
        embedded_name = str(record.get("embedded_name") or "")
        for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
            values = [int(frame[channel_index]) for frame in frames]
            counters[channel_index].update(values)
            if any(value != 0 for value in values):
                active_records[channel_index].append(embedded_name)
                unique_values = sorted(set(values))
                if len(unique_values) == 1:
                    active_constant_records[channel_index].append(embedded_name)
                    active_constant_values[channel_index].update([unique_values[0]])
                else:
                    append_sample(
                        per_record_varying_records[channel_index],
                        12,
                        {
                            "embedded_name": embedded_name,
                            "frame_count": len(values),
                            "distinct_values": unique_values[:12],
                        },
                    )

    profiles: dict[int, dict[str, object]] = {}
    for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
        counter = counters[channel_index]
        nonzero_count = sum(count for value, count in counter.items() if value != 0)
        profiles[channel_index] = {
            "sample_count": sum(counter.values()),
            "distinct_sample_count": len(counter),
            "nonzero_sample_count": nonzero_count,
            "active_record_count": len(active_records[channel_index]),
            "active_constant_record_count": len(active_constant_records[channel_index]),
            "per_record_varying_record_count": len(per_record_varying_records[channel_index]),
            "active_constant_values": [
                {"value": value, "record_count": count}
                for value, count in active_constant_values[channel_index].most_common(8)
            ],
            "per_record_varying_record_sample": per_record_varying_records[channel_index],
            "top_values": [
                {"value": value, "count": count}
                for value, count in counter.most_common(8)
            ],
            "active_record_sample": active_records[channel_index][:12],
        }
    return profiles


def zero_candidate_semantic_class(
    kind: str,
    bounds: dict[str, object],
    profile: dict[str, object],
) -> str:
    if kind == "always_zero_static":
        return "static_zero_padding_or_reserved"
    if kind == "low_cardinality_control_or_event":
        return "constant_per_record_control_tuple"
    minimum = int(bounds.get("min", 0) or 0)
    maximum = int(bounds.get("max", 0) or 0)
    distinct_count = int(bounds.get("distinct_sample_count", 0) or 0)
    if minimum >= 0 and maximum <= 255 and distinct_count <= 128:
        return "varying_quantized_control_or_index"
    active_record_count = int(profile.get("active_record_count", 0) or 0)
    if active_record_count > 0:
        return "varying_non_csab_curve_signal"
    return "unclassified_zero_candidate"


def build_unresolved_channel_worklist(
    channel_summary: list[dict[str, object]],
    stable_mappings: list[dict[str, object]],
    zero_candidate_records: list[dict[str, object]],
    unstable_candidate_records: list[dict[str, object]],
) -> list[dict[str, object]]:
    stable_channels = {int(record["anb_channel"]) for record in stable_mappings}
    zero_by_channel = {int(record["anb_channel"]): record for record in zero_candidate_records}
    unstable_by_channel = {int(record["anb_channel"]): record for record in unstable_candidate_records}
    worklist: list[dict[str, object]] = []
    for record in channel_summary:
        channel_index = int(record["anb_channel"])
        if channel_index in stable_channels:
            continue
        candidate_count = int(record.get("candidate_count", 0) or 0)
        if candidate_count > 0:
            best = record.get("best_candidate")
            unstable = unstable_by_channel.get(channel_index, {})
            worklist.append(
                {
                    "anb_channel": channel_index,
                    "work_kind": "candidate_but_not_stable",
                    "unstable_candidate_kind": unstable.get("kind"),
                    "unstable_candidate_semantic_class": unstable.get("semantic_class"),
                    "candidate_count": candidate_count,
                    "top_component": unstable.get("top_component"),
                    "top_component_count": unstable.get("top_component_count"),
                    "second_component_count": unstable.get("second_component_count"),
                    "top_component_share": unstable.get("top_component_share"),
                    "component_counts": record.get("component_counts", {}),
                    "mean_abs_correlation": record.get("mean_abs_correlation"),
                    "best_candidate": best,
                    "required_evidence": unstable.get(
                        "required_evidence",
                        "add matched ANB/CSAB records, channel-family constraints, or pose evidence until one component wins stable consensus",
                    ),
                }
            )
            continue
        zero = zero_by_channel.get(channel_index, {})
        zero_resolution_status = str(zero.get("resolution_status") or "")
        if zero_resolution_status.startswith("resolved_"):
            continue
        worklist.append(
            {
                "anb_channel": channel_index,
                "work_kind": "no_candidate",
                "candidate_count": 0,
                "zero_candidate_kind": zero.get("kind"),
                "zero_candidate_semantic_class": zero.get("semantic_class"),
                "zero_candidate_resolution_status": zero.get("resolution_status"),
                "min": zero.get("min"),
                "max": zero.get("max"),
                "distinct_sample_count": zero.get("distinct_sample_count"),
                "nonzero_sample_count": zero.get("nonzero_sample_count"),
                "active_record_count": zero.get("active_record_count"),
                "active_constant_record_count": zero.get("active_constant_record_count"),
                "per_record_varying_record_count": zero.get("per_record_varying_record_count"),
                "active_constant_values": zero.get("active_constant_values", []),
                "per_record_varying_record_sample": zero.get("per_record_varying_record_sample", []),
                "top_values": zero.get("top_values", []),
                "active_record_sample": zero.get("active_record_sample", []),
                "required_evidence": zero.get("required_evidence"),
            }
        )
    return worklist


def write_unresolved_channel_csv(path: Path, worklist: list[dict[str, object]]) -> None:
    fieldnames = [
        "anb_channel",
        "work_kind",
        "candidate_count",
        "unstable_candidate_kind",
        "unstable_candidate_semantic_class",
        "top_component",
        "top_component_count",
        "second_component_count",
        "top_component_share",
        "best_component",
        "best_correlation",
        "best_slope",
        "best_intercept",
        "best_max_abs_error",
        "best_sample_count",
        "mean_abs_correlation",
        "component_counts",
        "zero_candidate_kind",
        "zero_candidate_semantic_class",
        "zero_candidate_resolution_status",
        "min",
        "max",
        "distinct_sample_count",
        "nonzero_sample_count",
        "active_record_count",
        "active_constant_record_count",
        "per_record_varying_record_count",
        "active_constant_values",
        "per_record_varying_record_sample",
        "top_values",
        "active_record_sample",
        "required_evidence",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        for record in worklist:
            best = record.get("best_candidate") if isinstance(record.get("best_candidate"), dict) else {}
            component_counts = record.get("component_counts")
            writer.writerow(
                {
                    "anb_channel": record.get("anb_channel"),
                    "work_kind": record.get("work_kind"),
                    "candidate_count": record.get("candidate_count"),
                    "unstable_candidate_kind": record.get("unstable_candidate_kind"),
                    "unstable_candidate_semantic_class": record.get("unstable_candidate_semantic_class"),
                    "top_component": record.get("top_component"),
                    "top_component_count": record.get("top_component_count"),
                    "second_component_count": record.get("second_component_count"),
                    "top_component_share": record.get("top_component_share"),
                    "best_component": best.get("component"),
                    "best_correlation": best.get("correlation"),
                    "best_slope": best.get("slope"),
                    "best_intercept": best.get("intercept"),
                    "best_max_abs_error": best.get("max_abs_error"),
                    "best_sample_count": best.get("sample_count"),
                    "mean_abs_correlation": record.get("mean_abs_correlation"),
                    "component_counts": (
                        json.dumps(component_counts, sort_keys=True)
                        if isinstance(component_counts, dict)
                        else ""
                    ),
                    "zero_candidate_kind": record.get("zero_candidate_kind"),
                    "zero_candidate_semantic_class": record.get("zero_candidate_semantic_class"),
                    "zero_candidate_resolution_status": record.get("zero_candidate_resolution_status"),
                    "min": record.get("min"),
                    "max": record.get("max"),
                    "distinct_sample_count": record.get("distinct_sample_count"),
                    "nonzero_sample_count": record.get("nonzero_sample_count"),
                    "active_record_count": record.get("active_record_count"),
                    "active_constant_record_count": record.get("active_constant_record_count"),
                    "per_record_varying_record_count": record.get("per_record_varying_record_count"),
                    "active_constant_values": (
                        json.dumps(record.get("active_constant_values"), sort_keys=True)
                        if isinstance(record.get("active_constant_values"), list)
                        else ""
                    ),
                    "per_record_varying_record_sample": (
                        json.dumps(record.get("per_record_varying_record_sample"), sort_keys=True)
                        if isinstance(record.get("per_record_varying_record_sample"), list)
                        else ""
                    ),
                    "top_values": (
                        json.dumps(record.get("top_values"), sort_keys=True)
                        if isinstance(record.get("top_values"), list)
                        else ""
                    ),
                    "active_record_sample": (
                        json.dumps(record.get("active_record_sample"), sort_keys=True)
                        if isinstance(record.get("active_record_sample"), list)
                        else ""
                    ),
                    "required_evidence": record.get("required_evidence"),
                }
            )


def linear_fit(x: list[float], y: list[float]) -> dict[str, object] | None:
    if len(x) != len(y) or len(x) < 3:
        return None
    x_mean = mean(x)
    y_mean = mean(y)
    var_x = sum((value - x_mean) ** 2 for value in x)
    var_y = sum((value - y_mean) ** 2 for value in y)
    if var_x <= 0.0 or var_y <= 0.0:
        return None
    cov = sum((left - x_mean) * (right - y_mean) for left, right in zip(x, y))
    slope = cov / var_x
    intercept = y_mean - slope * x_mean
    correlation = cov / math.sqrt(var_x * var_y)
    max_abs_error = max(abs((slope * left + intercept) - right) for left, right in zip(x, y))
    return {
        "correlation": round_float(correlation),
        "slope": round_float(slope),
        "intercept": round_float(intercept),
        "max_abs_error": round_float(max_abs_error),
        "sample_count": len(x),
    }


def round_candidate(record: dict[str, object]) -> dict[str, object]:
    return {
        "component": record["component"],
        "correlation": record["correlation"],
        "slope": record["slope"],
        "intercept": record["intercept"],
        "max_abs_error": record["max_abs_error"],
        "sample_count": record["sample_count"],
    }


def read_json(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise ParseError(f"{path}: file not found")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ParseError(f"{path}: expected JSON object")
    return value


def append_sample(records: list[dict[str, object]], sample_limit: int, record: dict[str, object]) -> None:
    if len(records) < sample_limit:
        records.append(record)


def normalize_path(value: str) -> str:
    return value.replace("\\", "/").lower()


def signed16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def round_float(value: float, digits: int = 6) -> float:
    if not math.isfinite(value):
        return value
    return round(value, digits)
