from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from oot3d_asset_tool.native_corpus_promotion import (
    FORMAT,
    build_native_corpus_promotion,
    promote_native_corpus,
    validate_native_corpus_promotion,
)


def _git(repository: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _write_fixture_repository(root: Path) -> str:
    corpus = root / "build" / "analysis" / "decomp_batches"
    analysis = root / "analysis"
    corpus.mkdir(parents=True)
    analysis.mkdir()

    (analysis / "codebin_function_inventory.csv").write_text(
        "entry,name,maintained_name,module,logical_file\n"
        "0x00101000,Foo,Foo,game/test,src/oot3d/game/test/foo.cpp\n"
        "0x00102000,Bar,Bar,engine/test,src/oot3d/engine/test/bar.cpp\n"
        "0x00103000,Missing,Missing,game/test,src/oot3d/game/test/missing.cpp\n",
        encoding="utf-8",
    )
    (analysis / "codebin_fixture_data_symbols.csv").write_text(
        "address,old_name,new_name,kind,confidence,source_file,notes\n"
        "00500000,DAT_00500000,gFixtureWord,u32,high,fixture,fixture\n",
        encoding="utf-8",
    )
    (analysis / "codebin_fixture_struct_fields.csv").write_text(
        "structure,structure_size,offset,type,name,confidence,source,notes\n"
        "Oot3dActor,0x0008,0x0000,u32,tag,high,fixture,fixture\n"
        "Oot3dActor,0x0008,0x0000,s32,signed_tag,high,fixture,fixture\n"
        "Oot3dActor,0x0008,0x0004,Oot3dActor**,next,high,fixture,fixture\n",
        encoding="utf-8",
    )
    (analysis / "codebin_fixture_signatures.csv").write_text(
        "entry,name,return_type,param_types,param_names,confidence,source,notes\n"
        "0x00400000,ExternalService,void,Oot3dActor*,actor,high,fixture,fixture\n",
        encoding="utf-8",
    )
    (corpus / "fixture_probe.md").write_text(
        "# OoT3D decompiled function corpus\n\n"
        "## Foo\n\n"
        "- entry: `0x00101000`\n"
        "- decompile_status: ok\n\n"
        "### Decompiled C\n\n"
        "```c\nvoid Foo(Oot3dActor *actor) { actor = (Oot3dActor *)0; }\n```\n",
        encoding="utf-8",
    )
    typed = corpus / "fixture_typed.md"
    typed.write_text(
        "# OoT3D decompiled function corpus\n\n"
        "## Foo\n\n"
        "- entry: `0x00101000`\n"
        "- decompile_status: ok\n\n"
        "### Decompiled C\n\n"
        "```c\n"
        "void Foo(Oot3dActor *actor)\n"
        "{\n"
        "  if ((int)actor == 0) return;\n"
        "  if ((int)&(actor->tag) + 1 == 0) return;\n"
        "  actor->tag = 7;\n"
        "  actor->signed_tag = 7;\n"
        "  actor->next = actor->next;\n"
        "  ExternalService(actor);\n"
        "  FUN_00102000(actor);\n"
        "  DAT_00500000 = 1;\n"
        "}\n"
        "```\n\n"
        "## Bar\n\n"
        "- entry: `0x00102000`\n"
        "- decompile_status: ok\n\n"
        "### Decompiled C\n\n"
        "```c\n"
        "void Bar(Oot3dActor *actor)\n"
        "{\n"
        "  undefined4 value = 0;\n"
        "}\n"
        "```\n",
        encoding="utf-8",
    )

    _git(root, "init")
    _git(root, "config", "user.email", "fixture@example.invalid")
    _git(root, "config", "user.name", "Fixture")
    _git(root, "add", ".")
    _git(root, "commit", "-m", "fixture")
    revision = _git(root, "rev-parse", "HEAD")

    # The promoter must consume the committed blob, not this live edit.
    typed.write_text(typed.read_text(encoding="utf-8").replace(" = 1", " = 999"), encoding="utf-8")
    return revision


def _convert_fixture_corpus_to_sidecar(root: Path) -> str:
    _git(root, "rm", "--cached", "-r", "build")
    (root / ".gitignore").write_text("build/\n", encoding="utf-8")
    _git(root, "add", ".gitignore")
    _git(root, "commit", "-m", "keep generated corpus out of git")
    return _git(root, "rev-parse", "HEAD")


def _write_room_unit(path: Path) -> None:
    functions = [
        {
            "address": "0x00101000",
            "byte_length": 32,
            "family": "actor_test",
            "name": "Foo",
            "param_names": "",
            "param_types": "",
            "return_type": "void",
        },
        {
            "address": "0x00102000",
            "byte_length": 16,
            "family": "engine_test",
            "name": "Bar",
            "param_names": "actor",
            "param_types": "Oot3dActor*",
            "return_type": "void",
        },
        {
            "address": "0x00103000",
            "byte_length": 8,
            "family": "actor_test",
            "name": "Missing",
            "param_names": "actor",
            "param_types": "Oot3dActor*",
            "return_type": "void",
        },
    ]
    document = {
        "format": "oot3d_room_compilation_unit_v1",
        "identity": {
            "unit_id": "fixture:setup-0",
            "route_id": "fixture",
            "setup_index": 0,
            "payload_sha256": "a" * 64,
        },
        "actor_profiles": [
            {
                "actor_name": "ACTOR_FIXTURE",
                "behavior_graph": {
                    "functions": functions,
                    "consumer_call_edges": [
                        {
                            "caller_address": "0x00101000",
                            "caller_name": "Foo",
                            "callee_address": "0x00102000",
                            "callee_name": "Bar",
                            "relation": "direct_arm_call",
                        }
                    ],
                    "consumer_roots": [
                        {
                            "address": "0x00101000",
                            "name": "Foo",
                            "slot": "update",
                        }
                    ],
                },
            }
        ],
    }
    path.write_text(json.dumps(document), encoding="utf-8")


@pytest.mark.skipif(shutil.which("git") is None, reason="git is required")
def test_promoter_reads_pinned_commit_and_normalizes_symbols(tmp_path: Path) -> None:
    repository = tmp_path / "zelda3drecomp"
    repository.mkdir()
    revision = _write_fixture_repository(repository)
    room_unit = tmp_path / "room.json"
    _write_room_unit(room_unit)

    package = build_native_corpus_promotion(
        room_unit, repository, revision=revision
    )
    validate_native_corpus_promotion(package.manifest)

    counts = package.manifest["counts"]
    assert counts["profile_function_references"] == 3
    assert counts["unique_functions"] == 3
    assert counts["extracted_bodies"] == 2
    assert counts["typed_bodies"] == 2
    assert counts["missing_bodies"] == 1
    assert counts["missing_body_bytes"] == 8
    assert counts["consumer_roots_with_body"] == 1
    assert package.manifest["source"]["revision"] == revision
    assert package.manifest["source"]["mode"] == "git_archive_commit"
    assert package.manifest["source"]["worktree_changes_ignored"] == 1

    foo = next(
        item for item in package.manifest["functions"] if item["name"] == "Foo"
    )
    assert foo["body_typed"] is True
    assert foo["candidate_count"] == 2
    assert foo["body_variant_count"] == 2
    assert foo["hazards"] == []
    assert foo["compile_readiness"] == "requires_generated_layout_and_service_declarations"
    source = package.source_files[foo["output_source_path"]]
    assert "Bar(actor)" in source
    assert "gFixtureWord = 1" in source
    assert "gFixtureWord = 999" not in source
    assert "FUN_00102000" not in source
    assert "DAT_00500000" not in source
    assert "oot3d_host_address(actor) == 0" in source
    assert "oot3d_host_address(&(actor->tag)) + 1" in source
    assert "(int)actor" not in source
    assert any(
        row["kind"] == "host_pointer_arithmetic"
        for row in foo["symbol_replacements"]
    )
    assert foo["decompiled_signature"] == "void Foo(Oot3dActor *actor)"
    assert "void Foo(Oot3dActor *actor);" in package.support_header
    assert "void ExternalService(Oot3dActor* actor);" in package.support_header
    assert "using sbyte = std::int8_t;" in package.support_header
    assert "struct Oot3dActor {" in package.layout_header
    assert "u32 tag;" in package.layout_header
    assert "s32 signed_tag;" in package.layout_header
    assert "GuestPtr32<GuestPtr32<Oot3dActor>> next;" in package.layout_header
    assert "GuestPtr32& operator=(T* value);" in package.layout_header
    assert package.manifest["native_layouts"]["generated_structures"] == 1
    assert package.manifest["native_services"]["resolved_called_services"] == 1
    layout = package.manifest["native_layouts"]["structures"][0]
    assert layout["layout_mode"] == "anonymous_union_overlay"

    bar = next(
        item for item in package.manifest["functions"] if item["name"] == "Bar"
    )
    assert bar["hazards"] == ["ghidra_undefined_types"]
    missing = next(
        item for item in package.manifest["functions"] if item["name"] == "Missing"
    )
    assert missing["output_source_path"] is None
    assert missing["compile_readiness"] == "missing_decompiled_body"


@pytest.mark.skipif(shutil.which("git") is None, reason="git is required")
def test_promoter_output_is_deterministic_and_safely_replaceable(tmp_path: Path) -> None:
    repository = tmp_path / "zelda3drecomp"
    repository.mkdir()
    revision = _write_fixture_repository(repository)
    room_unit = tmp_path / "room.json"
    _write_room_unit(room_unit)
    first = tmp_path / "first"
    second = tmp_path / "second"

    package_a = promote_native_corpus(
        room_unit, repository, first, revision=revision
    )
    package_b = promote_native_corpus(
        room_unit, repository, second, revision=revision
    )

    assert package_a.manifest["format"] == FORMAT
    assert package_a.manifest["payload_sha256"] == package_b.manifest["payload_sha256"]
    assert (first / "manifest.json").read_bytes() == (
        second / "manifest.json"
    ).read_bytes()
    assert (first / "report.md").read_bytes() == (second / "report.md").read_bytes()

    replaced = promote_native_corpus(
        room_unit, repository, first, revision=revision, replace=True
    )
    assert replaced.manifest["payload_sha256"] == package_a.manifest["payload_sha256"]


@pytest.mark.skipif(shutil.which("git") is None, reason="git is required")
def test_promoter_hashes_ignored_generated_corpus_sidecar(tmp_path: Path) -> None:
    repository = tmp_path / "zelda3drecomp"
    repository.mkdir()
    _write_fixture_repository(repository)
    revision = _convert_fixture_corpus_to_sidecar(repository)
    room_unit = tmp_path / "room.json"
    _write_room_unit(room_unit)

    package = build_native_corpus_promotion(room_unit, repository, revision=revision)

    source = package.manifest["source"]
    assert source["mode"] == "git_commit_plus_generated_corpus_sidecar"
    assert source["committed_evidence_mode"] == "git_archive_commit"
    assert source["generated_corpus"]["file_count"] == 2
    assert len(source["generated_corpus"]["aggregate_sha256"]) == 64
    assert package.manifest["policy"]["tracked_source_worktree_consumed"] is False
    assert package.manifest["policy"]["generated_corpus_sidecar_consumed"] is True
    assert package.manifest["counts"]["extracted_bodies"] == 2
