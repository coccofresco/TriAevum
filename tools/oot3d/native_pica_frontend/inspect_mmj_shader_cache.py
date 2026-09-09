"""Inspect MMJ's text shader corpus without activating foreign renderer code.

Unlike Citra transferable .bin files, these .shader files carry generated GLSL,
opaque configuration references and linked stage triples, not native PICA state.
Outputs are private evidence, never an effective TriAevum shader inventory.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


STAGES = {0x8B31: "vertex", 0x8B30: "fragment", 0x8DD9: "geometry"}
SUFFIXES = {"vertex": "vert", "fragment": "frag", "geometry": "geom"}
MAX_BYTES = 64 * 1024 * 1024
MAX_RECORDS = 65536
MARKER = re.compile(r"// (shader|reference|program): (.+)")
HEX = re.compile(r"[0-9A-Fa-f]{1,16}")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class Shader:
    identifier: int
    stage: str
    source: str

    @property
    def identity(self) -> tuple[str, str]:
        return self.stage, sha256(self.source.encode("utf-8"))


@dataclass
class Corpus:
    shaders: dict[int, Shader]
    references: list[tuple[int, int]]
    programs: list[tuple[int, int, int]]
    metadata: dict[str, int]


def parse_metadata(text: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        key, separator, value = line.partition(":")
        if not separator or key not in {"version", "shader", "reference", "program"}:
            raise ValueError("unknown MMJ metadata field")
        if key in result or not re.fullmatch(r"[0-9]+", value.strip()):
            raise ValueError("duplicate or invalid MMJ metadata field")
        result[key] = int(value)
    if set(result) != {"version", "shader", "reference", "program"}:
        raise ValueError("incomplete MMJ metadata")
    if result["version"] not in {8, 26}:
        raise ValueError("unreviewed MMJ metadata version")
    if any(value > MAX_RECORDS for value in result.values()):
        raise ValueError("MMJ metadata count exceeds limit")
    return result


def parse_corpus(text: str, metadata_text: str) -> Corpus:
    if len(text.encode("utf-8")) > MAX_BYTES:
        raise ValueError("MMJ corpus exceeds size limit")
    metadata = parse_metadata(metadata_text)
    shaders: dict[int, Shader] = {}
    references = []
    programs = []
    counts = {"shader": 0, "reference": 0, "program": 0}
    pending: tuple[int, str] | None = None
    body: list[str] = []

    def finish_shader() -> None:
        if pending is None:
            return
        identifier, stage = pending
        source = "\n".join(body).strip() + "\n"
        if not source.strip():
            raise ValueError("empty MMJ shader")
        shader = Shader(identifier, stage, source)
        previous = shaders.get(identifier)
        if previous is not None and previous != shader:
            raise ValueError("MMJ source identifier collision")
        shaders[identifier] = shader

    for number, line in enumerate(text.splitlines(), 1):
        match = MARKER.fullmatch(line)
        if match is None:
            if line.startswith(("// shader:", "// reference:", "// program:")):
                raise ValueError(f"malformed MMJ marker at line {number}")
            if pending is not None:
                body.append(line)
            elif line.strip():
                raise ValueError(f"text outside shader at line {number}")
            continue
        finish_shader()
        pending = None
        body = []
        kind, values = match.groups()
        fields = [value.strip() for value in values.split(",")]
        expected = 3 if kind == "program" else 2
        if len(fields) != expected or not all(HEX.fullmatch(field) for field in fields):
            raise ValueError(f"invalid MMJ {kind} at line {number}")
        values = tuple(int(field, 16) for field in fields)
        counts[kind] += 1
        if counts[kind] > MAX_RECORDS:
            raise ValueError("MMJ record count exceeds limit")
        if kind == "shader":
            stage, identifier = values
            if stage not in STAGES or identifier == 0:
                raise ValueError("unsupported MMJ stage or null source identifier")
            pending = identifier, STAGES[stage]
        elif kind == "reference":
            references.append(values)
        else:
            programs.append(values)
    finish_shader()

    if any(counts[kind] != metadata[kind] for kind in counts):
        raise ValueError(f"MMJ metadata/count mismatch: parsed {counts}")
    if not shaders or not programs:
        raise ValueError("empty MMJ corpus")
    configurations: dict[int, int] = {}
    for config, source in references:
        if source not in shaders:
            raise ValueError("MMJ reference points to missing shader")
        if config in configurations and configurations[config] != source:
            raise ValueError("MMJ configuration reference collision")
        configurations[config] = source
    for program in programs:
        for identifier, stage in zip(program, ("vertex", "geometry", "fragment")):
            if identifier == 0 and stage == "geometry":
                continue
            if identifier not in shaders or shaders[identifier].stage != stage:
                raise ValueError("MMJ program has missing or mismatched stage")
    return Corpus(shaders, references, programs, metadata)


def read_bounded(path: Path, limit: int) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"input exceeds limit: {path}")
    return data


def describe(corpus: Corpus) -> dict:
    stages = {stage: sum(shader.stage == stage for shader in corpus.shaders.values())
              for stage in STAGES.values()}
    entries = []
    for shader in corpus.shaders.values():
        # These are source observations, not claims of reachable game behavior.
        white_stubs = re.findall(
            r"vec4\s+(shadowTexture(?:Cube)?)\s*\([^)]*\)\s*"
            r"\{\s*return\s+vec4\(1\.0\);\s*\}", shader.source)
        entries.append({
            "mmj_source_identifier": f"{shader.identifier:016X}",
            "stage": shader.stage,
            "source_sha256": shader.identity[1],
            "source_bytes": len(shader.source.encode("utf-8")),
            "white_shadow_helper_definitions": white_stubs,
            "shadow_helper_name_occurrences": {
                name: len(re.findall(r"\b" + name + r"\s*\(", shader.source))
                for name in white_stubs},
        })
    identities = {shader.identity for shader in corpus.shaders.values()}
    return {
        "metadata": corpus.metadata,
        "unique_shader_identifiers": len(corpus.shaders),
        "unique_stage_sources": len(identities),
        "stage_counts": stages,
        "unique_reference_pairs": len(set(corpus.references)),
        "unique_program_triples": len(set(corpus.programs)),
        "shaders": entries,
        "references": [[f"{value:016X}" for value in pair] for pair in corpus.references],
        "programs": [[f"{value:016X}" for value in triple] for triple in corpus.programs],
        "native_pica_registers_available": False,
        "native_vertex_bytecode_available": False,
        "full_nri_pipeline_recipes_available": False,
        "runtime_binding_validated": False,
        "canonical_shader_modules_added": 0,
    }


def validate_programs(corpus: Corpus, validator: str) -> dict:
    """Compile/link donor desktop GLSL, without rewriting its ABI or semantics."""
    results = []
    with tempfile.TemporaryDirectory(prefix="triaevum-mmj-glsl-") as temporary:
        directory = Path(temporary)
        paths = {}
        for shader in corpus.shaders.values():
            path = directory / f"{shader.identifier:016X}.{SUFFIXES[shader.stage]}"
            source = shader.source
            if not re.search(r"^\s*#\s*version\b", source, re.MULTILINE):
                source = "#version 450 core\n" + source
            path.write_text(source, encoding="utf-8")
            paths[shader.identifier] = path
        for triple in sorted(set(corpus.programs)):
            command = [validator, "-l", *(str(paths[value]) for value in triple if value)]
            run = subprocess.run(command, capture_output=True, text=True, timeout=30)
            results.append({
                "program": [f"{value:016X}" for value in triple],
                "returncode": run.returncode,
                "diagnostic": (run.stdout + run.stderr)[-8192:] if run.returncode else "",
            })
    return {
        "mode": "offline_desktop_glsl_450_compile_and_link_not_vulkan_or_gpu",
        "program_count": len(results),
        "passed": sum(item["returncode"] == 0 for item in results),
        "failed": sum(item["returncode"] != 0 for item in results),
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", type=Path, required=True,
                        help="MMJ .shader file; its adjacent .meta is required")
    parser.add_argument("--output", type=Path, required=True,
                        help="Private JSON evidence report (contains donor identifiers)")
    parser.add_argument("--validator", help="Optional glslangValidator executable")
    args = parser.parse_args()
    protected = {path.resolve() for path in args.input}
    protected.update(Path(str(path) + ".meta").resolve() for path in args.input)
    if args.output.resolve() in protected:
        parser.error("output must not overwrite an input or metadata file")
    records = []
    corpora = []
    linked = {}
    for path in args.input:
        data = read_bounded(path, MAX_BYTES)
        meta = read_bounded(Path(str(path) + ".meta"), 4096)
        corpus = parse_corpus(data.decode("utf-8"), meta.decode("utf-8"))
        record = {"path": str(path), "bytes": len(data), "sha256": sha256(data),
                  "metadata_sha256": sha256(meta), **describe(corpus)}
        if args.validator:
            # Regional duplicates require only one compilation of identical input.
            key = sha256(data)
            if key not in linked:
                linked[key] = validate_programs(corpus, args.validator)
            record["source_validation"] = linked[key]
        records.append(record)
        corpora.append(corpus)
    identities = {shader.identity for corpus in corpora for shader in corpus.shaders.values()}
    program_identities = {
        tuple(corpus.shaders[value].identity if value else None for value in triple)
        for corpus in corpora for triple in corpus.programs}
    intersections = []
    for i, first in enumerate(corpora):
        for j in range(i + 1, len(corpora)):
            second = corpora[j]
            shared = ({shader.identity for shader in first.shaders.values()} &
                      {shader.identity for shader in second.shaders.values()})
            intersections.append({"input_a": i, "input_b": j,
                                  "identical_stage_sources": len(shared)})
    report = {
        "format": "triaevum_mmj_glsl_evidence_v1",
        "scope": "foreign_generated_glsl_and_observed_stage_links_not_runtime_seed",
        "inputs": records,
        "unique_files": len({item["sha256"] for item in records}),
        "union_stage_sources": len(identities),
        "union_program_source_triples": len(program_identities),
        "exact_source_intersections": intersections,
        "semantic_equivalence_proven": False,
        "game_coverage_proven": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in
                      ("unique_files", "union_stage_sources", "union_program_source_triples")}))
    return 1 if any(item["failed"] for item in linked.values()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
