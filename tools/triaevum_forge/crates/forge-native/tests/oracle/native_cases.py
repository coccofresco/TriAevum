# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs one Python Forge function for the forge-native oracle tests.

Usage: native_cases.py RELEASE_TOOLS CASE ARGUMENTS.json OUTCOME.json
Writes {"ok": result} or {"error": str(exc), "type": class name}.
"""

import json
import sys
from pathlib import Path

tools = Path(sys.argv[1])
sys.path.insert(0, str(tools))
sys.path.insert(1, str(tools.parents[1]))

import device_pipeline_preparation  # noqa: E402
import game_language  # noqa: E402
import installed_runtime  # noqa: E402
import product_contract  # noqa: E402
import shader_corpus_layout  # noqa: E402
import shader_corpus_qualification as qualification  # noqa: E402
import shader_preparation  # noqa: E402
from common import atomic_write_json, sha256_file  # noqa: E402


def optional_path(value):
    return None if value is None else Path(value)


def with_events(function, **arguments):
    events = []
    result = function(**arguments, report=lambda stage, message: events.append([stage, message]))
    return {"result": result, "events": events}


def query_product(a):
    return product_contract.query_product(Path(a["executable"]), a.get("source_commit", ""),
                                          plugin=optional_path(a.get("plugin")))


def validate_product_info(a):
    return product_contract.validate_product_info(a["info"], a.get("source_commit", ""))


def ensure_runtime_config(a):
    return product_contract.ensure_runtime_config(Path(a["path"]), a["product"])


def language_discover(a):
    return game_language.discover(Path(a["runtime"]), Path(a["romfs"]))


def language_install(a):
    return game_language.install(Path(a["data_root"]), a["detected"])


def language_select(a):
    return game_language.select(Path(a["data_root"]), a["code"])


def language_validate(a):
    return game_language.validate(a["document"])


def prepare_shader_seed(a):
    outcome = with_events(shader_preparation.prepare_shader_seed, root=Path(a["root"]),
                          data_root=Path(a["data_root"]), title=a["title"])
    outcome["result"] = None if outcome["result"] is None else str(outcome["result"])
    return outcome


def prepare_renderer(a):
    return with_events(shader_preparation.prepare_renderer_shader_cache, root=Path(a["root"]),
                       data_root=Path(a["data_root"]), title=a["title"], cache_directory=Path(a["cache"]))


def prepare_device(a):
    cancel = a.get("cancel", False)
    return with_events(device_pipeline_preparation.prepare_device_pipelines, root=Path(a["root"]),
                       data_root=Path(a["data_root"]), title=a["title"], pack=optional_path(a.get("pack")),
                       cache_directory=optional_path(a.get("cache")), cancel=lambda: cancel)


def adopt_existing_cache(a):
    return with_events(device_pipeline_preparation.adopt_existing_cache, destination=Path(a["destination"]),
                       source=optional_path(a.get("source")))


def validate_installed(a):
    return str(installed_runtime.validate_installed_runtime(
        Path(a["executable"]), Path(a["title"]), Path(a["data_root"]), a["runtime"]))


def read_portable_corpus(a):
    schema, modules = shader_corpus_layout.read_portable_corpus(Path(a["pack"]))
    return [schema, len(modules)]


def bind_shader_corpus(a):
    catalog, items = shader_corpus_layout.bind_shader_corpus(
        a["catalog"], Path(a["pack"]), [Path(p) for p in a["manifests"]], Path(a["helper"]),
        [Path(p) for p in a.get("dependencies", [])], qualification=Path(a["qualification"]),
        source_root=Path(a["source_root"]))
    return [catalog, items]


def validate_bundled_corpus(a):
    return shader_corpus_layout.validate_bundled_corpus(Path(a["root"]), a["catalog"], a["declared"])


def json_error(a):
    return json.loads(a["text"])


def make_qualification(a):
    """The test fixture of test_shader_corpus_layout.write_qualification."""
    sources = Path(a["sources"])

    def inputs(names):
        files = {name: sha256_file(sources / name) for name in names}
        return dict(format=qualification.INPUTS_FORMAT, targets=["fixture"],
                    sha256=qualification.inputs_digest(files), files=files)
    atomic_write_json(Path(a["output"]), qualification.qualification_record(
        generator=inputs(a["generator"]), policies=[inputs(a["runtime"])],
        pack_sha256=sha256_file(Path(a["pack"])),
        manifest_sha256=[sha256_file(Path(p)) for p in a["manifests"]], modules=1, requests=1))


CASES = {name: value for name, value in globals().items() if callable(value) and not name.startswith("_")
         and name not in ("optional_path", "with_events", "atomic_write_json", "sha256_file", "Path")}


def main():
    case, arguments_path, outcome_path = sys.argv[2:5]
    arguments = json.loads(Path(arguments_path).read_text(encoding="utf-8"))
    try:
        outcome = {"ok": CASES[case](arguments)}
    except Exception as exc:  # the oracle reports every failure class
        outcome = {"error": str(exc), "type": type(exc).__name__}
    Path(outcome_path).write_text(json.dumps(outcome), encoding="utf-8")


if __name__ == "__main__":
    main()
