"""Linux release package of a title from the same release's audited Windows package (walks its manifest).

The reference is the audited Windows package of that release (its platform-neutral files: resources, docs, licences,
recipes, corpus, title sources); every Windows binary is replaced by the Linux build of the same commit, the
precompiled catalog is rebound (linux_precompiled_catalog.create_catalog), the shader corpus is bound with the device
pipeline helper, and package_release writes the package (it audits it). The runtime's embedded commit must be the
release's; new files under shared prefixes come from walking the release's own manifest; a reused OoT3D Linux .so keeps
the platform_build/sources of the catalog it was built for, after asserting the translated files are identical.

Nothing here boots a game. Run it from the release commit's clean source root; the Steam Runtime 4 SDK the Linux
binaries were built in is --sdk or TRIAEVUM_STEAMRT4_SDK:
  python3 tools/triaevum_release/package_linux_release.py --title mm3d --version 0.5.0-alpha.1 \
      --reference <Windows package> --out <new dir> --commit <40 hex> --runtime <Linux runtime build dir> \
      (--helper-runtime | --helper <triaevum_nri_pipeline_prepare>) --forge <rust_forge_build output> \
      --title-so <Linux title .so> [--reuse-linux-title-from <older Linux package>] [--candidate]
--helper-runtime: the game executable runs Forge's device pipeline job (--pipeline-prepare), so the driver's
per-application pipeline cache is the game's own.
"""
import multiprocessing as _mp; _mp.set_start_method('fork', force=True)  # s2: py3.14 forkserver re-imports this unguarded script
import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("--title", choices=("oot3d", "mm3d"), required=True)
ap.add_argument("--version", required=True)
ap.add_argument("--reference", type=Path, required=True)
ap.add_argument("--out", type=Path, required=True)
ap.add_argument("--commit", required=True)
ap.add_argument("--runtime", type=Path, required=True)
hg = ap.add_mutually_exclusive_group(required=True)
hg.add_argument("--helper", type=Path)
hg.add_argument("--helper-runtime", action="store_true", help="the game executable runs the device job (--pipeline-prepare)")
ap.add_argument("--forge", type=Path, required=True)
ap.add_argument("--title-so", type=Path, required=True)
ap.add_argument("--reuse-linux-title-from", type=Path)
ap.add_argument("--sdk", type=Path, help="the Steam Runtime 4 SDK (default: environment TRIAEVUM_STEAMRT4_SDK)")
ap.add_argument("--candidate", action="store_true")
ap.add_argument("--dry-run", action="store_true", help="check every input and print the layout; write nothing")
a = ap.parse_args()
if a.sdk is None:
    import os
    if not os.environ.get("TRIAEVUM_STEAMRT4_SDK"):
        ap.error("--sdk or TRIAEVUM_STEAMRT4_SDK is required")
    a.sdk = Path(os.environ["TRIAEVUM_STEAMRT4_SDK"])

repo = Path.cwd()
sys.path.insert(0, str(repo))
from tools.triaevum_release.common import atomic_write_json, load_json_object, sha256_file  # noqa: E402
from tools.triaevum_release.release_platform import LINUX, for_title  # noqa: E402
from tools.triaevum_release.rust_forge_build import RECEIPT, BINARIES  # noqa: E402
from tools.triaevum_release.stage_linux_forge_candidate import require_title_forge  # noqa: E402

assert len(a.commit) == 40, "full commit id"
if (repo / "SOURCE_ARCHIVE_MANIFEST.json").is_file():
    manifest_commit = json.loads((repo / "SOURCE_ARCHIVE_MANIFEST.json").read_text())["source_commit"]
else:  # a git checkout instead of an extracted source archive: the commit is HEAD of a clean tree
    import subprocess
    manifest_commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    assert not subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=no"], text=True).strip(), "source tree has local changes"
assert manifest_commit == a.commit, f"source tree is {manifest_commit}, not {a.commit}"
ref = a.reference
reference = load_json_object(next(p for p in (ref / "release-manifest.json", ref / "docs/release-manifest.json") if p.is_file()))
assert reference["release"].get("version", a.version) == a.version, ("reference version", reference["release"].get("version"))
require_title_forge(a.forge, a.title)
forge_receipt = load_json_object(a.forge / RECEIPT)
assert forge_receipt["source_commit"] == a.commit, ("Forge source commit", forge_receipt["source_commit"])
product = for_title(a.title, LINUX)
SDK_LIB = a.sdk / "usr/lib/x86_64-linux-gnu"
BUILD_ID = next(line.split("=", 1)[1].strip().strip('"') for line in
                (a.sdk / "usr/lib/os-release").read_text().splitlines() if line.startswith("BUILD_ID="))
assert BUILD_ID == "4.0.20260805.254769", BUILD_ID

ref_catalog = load_json_object(ref / "recipes/precompiled-titles.json")
plugin_rel = ref_catalog["titles"][0]["plugin"]["path"]
assert plugin_rel.startswith("titles/") and plugin_rel.endswith(".dll"), plugin_rel
plugin_rel = plugin_rel[:-4] + ".so"
binaries = {
    LINUX.runtime: (a.runtime / LINUX.runtime, "runtime_executable"),
    LINUX.title_module: (a.runtime / LINUX.title_module, "runtime_library"),
    "libtriaevum_immerse.so": (a.runtime / "libtriaevum_immerse.so", "separately_licensed_module"),
    "libtriaevum_dlss.so": (a.runtime / "libtriaevum_dlss.so", "separately_licensed_module"),
    "libnvidia-ngx-dlss.so.310.7.0": (a.runtime / "libnvidia-ngx-dlss.so.310.7.0", "separately_licensed_module"),
    LINUX.native_module: (a.runtime / "oot3d_game_module.so", "forge_runtime_module"),
    plugin_rel: (a.title_so, "precompiled_title"),
    "forge/oot3d_native_pica_aot_compiler": (a.runtime / "oot3d_native_pica_aot_compiler", "shader_preparation_tool"),
    "forge/libshaderc.so.1": (SDK_LIB / "libshaderc.so.1", "forge_runtime_module"),
}
for name in ("libbz2.so.1.0", "libfmt.so.10", "libSDL2-2.0.so.0", "libSDL3.so.0", "libshaderc.so.1",
             "libspdlog.so.1.15", "libtinyxml2.so.11", "libzip.so.5", "libdecor-0.so.0", "libXtst.so.6",
             "libfribidi.so.0", "liblzma.so.5", "libzstd.so.1", "libcrypto.so.3", "libz.so.1"):
    binaries[f"lib/{name}"] = (SDK_LIB / name, "runtime_library")
for name in ("libstdc++.so.6", "libgcc_s.so.1"):
    binaries[f"lib/compat-cxx/{name}"] = (SDK_LIB / name, "runtime_library")
for path_rel, role in BINARIES[LINUX.target].values():
    binaries[path_rel] = (a.forge / path_rel, role)
missing = [str(src) for src, _ in binaries.values() if not src.is_file()] + \
          [str(p) for p in ((a.helper,) if a.helper else ()) if not p.is_file()]
# Roles the reference carries as Windows binaries or that are bound anew here.
REBOUND = {"precompiled_catalog", "portable_shader_corpus", "portable_pipeline_recipes", "translated_title_source",
           "title_build_source", "precomputed_interior_sun", "portable_shader_qualification", "precomputed_scene_lights"}
BINARY_ROLES = {"runtime_executable", "runtime_library", "separately_licensed_module", "forge_runtime_module",
                "precompiled_title", "shader_preparation_tool", "forge_executable", "forge_command_line",
                "forge_update_helper", "forge_build_receipt", "forge_frozen_resource", "optional_vulkan_runtime",
                "forge_clang_header"}
WINDOWS_SUFFIXES = (".exe", ".dll", ".lib", ".pdb")
skipped = [e for e in reference["files"] if e["role"] in REBOUND | BINARY_ROLES or e["path"].lower().endswith(WINDOWS_SUFFIXES)]
carried = [e for e in reference["files"] if e not in skipped]
missing += [e["path"] for e in carried if not (ref / e["path"]).is_file()]
print(json.dumps({"title": a.title, "installed_runtime": product.installed_runtime, "version": a.version,
                  "carried": len(carried), "skipped_roles": sorted({e["role"] for e in skipped}),
                  "skipped_non_binary_roles": sorted({e["role"] for e in skipped} - BINARY_ROLES - REBOUND),
                  "binaries": sorted(binaries), "missing": missing}, indent=1))
if missing:
    sys.exit("missing inputs")
if a.dry_run:
    sys.exit(0)

from tools.triaevum_release.linux_precompiled_catalog import create_catalog  # noqa: E402
from tools.triaevum_release.shader_corpus_layout import bind_shader_corpus  # noqa: E402
from tools.triaevum_release.precomputed_tables import bind_precomputed_tables  # noqa: E402
from tools.triaevum_release.precompiled_title_layout import artifact  # noqa: E402
from tools.triaevum_release.package_release import package_release  # noqa: E402
from tools.triaevum_release.rust_forge_notices import NOTICES, NOTICES_RELATIVE  # noqa: E402

out = a.out
assert not out.exists(), "output must not exist"
stage, package = out / "stage", out / "package"
for relative, (source, _role) in binaries.items():
    target = stage / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source.resolve(), target)
    target.chmod(0o755)
# create_catalog finds the runtime under its installed product name.
shutil.copyfile(stage / LINUX.runtime, stage / product.installed_runtime)
(stage / product.installed_runtime).chmod(0o755)
import os  # noqa: E402
os.environ["LD_LIBRARY_PATH"] = str(stage / "lib")
catalog = create_catalog(ref, stage, stage / plugin_rel, source_commit=a.commit,
                         shader_compiler=stage / "forge/oot3d_native_pica_aot_compiler",
                         shader_dependencies=[stage / "forge/libshaderc.so.1"])
(stage / product.installed_runtime).unlink()
corpus = ref / "forge/shader-corpus"
pipelines = sorted(corpus.glob("pipelines-*.json"))
catalog, corpus_items = bind_shader_corpus(catalog, corpus / "portable.o3ps", pipelines, None if a.helper_runtime else a.helper,
                                           qualification=corpus / "qualification.json", source_root=repo)
from tools.triaevum_release.source_archive import create_source_archive  # noqa: E402
cut_source_zip = stage / "source/TriAevum-source.zip"
cut_source_zip.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(ref / "source/TriAevum-source.zip", cut_source_zip)
windows_translated = next((ref / "source/titles").glob("*-translated.zip"))
prefix = a.title
linux_translated = stage / f"source/titles/{prefix}-linux-translated.zip"
linux_build = stage / f"source/titles/{prefix}-linux-build.zip"
linux_translated.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(windows_translated) as source:
    original = json.loads(source.read("TITLE_SOURCE_MANIFEST.json"))
files = original["files"]
if a.reuse_linux_title_from:
    # OoT3D: the Linux .so of an earlier release, valid only if its translated sources are this release's.
    old = a.reuse_linux_title_from
    old_catalog = load_json_object(old / "recipes/precompiled-titles.json")
    old_title = old_catalog["titles"][0]
    assert sha256_file(a.title_so) == old_title["plugin"]["sha256"], "the reused .so is not the old catalog's plugin"
    old_zip = old / f"source/titles/{prefix}-linux-translated.zip"
    with zipfile.ZipFile(old_zip) as z:
        old_files = json.loads(z.read("TITLE_SOURCE_MANIFEST.json"))["files"]
    if old_files != files:
        common = set(old_files) & set(files)
        diff = sorted(n for n in common if old_files[n] != files[n])
        sys.exit(f"translated sources differ from the reused Linux .so's: {len(diff)} changed, "
                 f"{len(set(files) ^ set(old_files))} added/removed -> rebuild the Linux title from {windows_translated.name}")
    shutil.copyfile(old_zip, linux_translated)
    shutil.copyfile(old / f"source/titles/{prefix}-linux-build.zip", linux_build)
    platform_build, build_commit = old_title["platform_build"], old_title["build_source_commit"]
else:
    shutil.copyfile(cut_source_zip, linux_build)
    receipt = {"status": "built", "backend": "linux_platform_rebuild_v1",
               "plugin_sha256": sha256_file(stage / plugin_rel), "plugin_bytes": (stage / plugin_rel).stat().st_size,
               "code_sha256": original["build"]["code_sha256"], "program_sha256": original["build"]["program_sha256"],
               "translator_identity_sha256": original["build"]["translator_identity_sha256"],
               "target": LINUX.target, "build_target": "tools/triaevum_release/linux_title",
               "sdk": f"Steam Runtime 4 / {BUILD_ID}",
               "verification": "Built from these translated sources in the SDK (tools/triaevum_release/linux_title)",
               "translated_inputs": len(files),
               "input_manifest_sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()}
    manifest = {"format": "triaevum_translated_title_source_v1", "recipe": ref_catalog["titles"][0]["recipe"],
                "build_source_commit": a.commit, "build": receipt, "files": files,
                "original_translation_build": original["build"],
                "original_translation_build_source_commit": original["build_source_commit"]}
    with zipfile.ZipFile(windows_translated) as source, \
            zipfile.ZipFile(linux_translated, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in sorted(files):
            data = source.read(name)
            assert hashlib.sha256(data).hexdigest() == files[name], name
            archive.writestr(name, data)
        archive.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps(manifest, indent=2))
    platform_build = {key: receipt[key] for key in (
        "backend", "build_target", "code_sha256", "input_manifest_sha256", "plugin_bytes", "plugin_sha256",
        "program_sha256", "sdk", "status", "target", "translated_inputs", "translator_identity_sha256", "verification")}
    build_commit = a.commit
records = [artifact(linux_translated, f"source/titles/{prefix}-linux-translated.zip"),
           artifact(linux_build, f"source/titles/{prefix}-linux-build.zip")]
for title in catalog["titles"]:
    title["sources"], title["build_source_commit"], title["platform_build"] = records, build_commit, platform_build
table_items = []
if (ref / "forge/precomputed/interior-sun.masks").is_file():
    catalog, table_items = bind_precomputed_tables(catalog, ref / "forge/precomputed/interior-sun.masks",
                                                   ref / "forge/precomputed/scene-lights.json")
atomic_write_json(stage / "recipes/precompiled-titles.json", catalog)

items = {}
SDK = a.sdk
notices = stage / "LICENSES/linux"
(notices / "common-licenses").mkdir(parents=True, exist_ok=True)
PACKAGES = {
    "libstdc++6": "libstdc++.so.6", "libgcc-s1": "libgcc_s.so.1", "libdecor-0-0": "libdecor-0.so.0",
    "libxtst6": "libXtst.so.6", "libfribidi0": "libfribidi.so.0", "liblzma5": "liblzma.so.5",
    "libzstd1": "libzstd.so.1", "libssl3t64": "libcrypto.so.3", "zlib1g": "libz.so.1",
    "libfmt10": "libfmt.so.10", "libspdlog1.15": "libspdlog.so.1.15", "libtinyxml2-11": "libtinyxml2.so.11",
    "libzip5": "libzip.so.5", "libbz2-1.0": "libbz2.so.1.0", "libsdl3-0": "libSDL3.so.0",
    "libsdl2-2.0-0": "libSDL2-2.0.so.0", "libshaderc1": "libshaderc.so.1",
}
for package_name in PACKAGES:
    shutil.copyfile(SDK / f"usr/share/doc/{package_name}/copyright", notices / f"{package_name}.copyright")
for name in ("GPL-2", "GPL-3", "LGPL-2.1", "Apache-2.0", "CC0-1.0", "Artistic", "GPL-1"):
    shutil.copyfile(SDK / f"usr/share/common-licenses/{name}", notices / "common-licenses" / name)
rows = "\n".join(f"| `{lib}` | `{pkg}` | `{pkg}.copyright` |" for pkg, lib in PACKAGES.items())
(notices / "README.md").write_text(
    "# Linux runtime libraries\n\n"
    f"The Linux packages bundle these shared libraries from the Steam Runtime 4 SDK, build `{BUILD_ID}`, unmodified. "
    "`lib/compat-cxx/` (libstdc++, libgcc_s) is used only when the system's libstdc++ is missing or older. Each library "
    "keeps its own licence, given in the Debian copyright file of its package; licences those files reference by name "
    "are in `common-licenses/`.\n\n| Library | Package | Copyright file |\n|---|---|---|\n" + rows + "\n\n"
    "The complete corresponding source of every package of this runtime build, including these, is published by Valve at "
    f"https://repo.steampowered.com/steamrt4/images/{BUILD_ID}/sources/ (a Debian source archive per package, with its patches).\n")
for path in sorted(notices.rglob("*")):
    if path.is_file():
        rel = path.relative_to(stage).as_posix()
        items[rel] = dict(source=str(path), path=rel, role="license")
for entry in carried:
    items[entry["path"]] = dict(source=str(ref / entry["path"]), path=entry["path"], role=entry["role"])
items["source/TriAevum-source.zip"] = dict(source=str(cut_source_zip), path="source/TriAevum-source.zip", role="corresponding_source")
for name in ("LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md", "SOURCE_OFFER.md"):
    items.setdefault(name, dict(source=str(repo / name), path=name, role="documentation"))
for relative, (_source, role) in binaries.items():
    items[relative] = dict(source=str(stage / relative), path=relative, role=role)
items[RECEIPT] = dict(source=str(a.forge / RECEIPT), path=RECEIPT, role="forge_build_receipt")
items[NOTICES_RELATIVE] = dict(source=str(NOTICES), path=NOTICES_RELATIVE, role="license")
for item in corpus_items + table_items:
    items[item["path"]] = item
for record in records:
    items[record["path"]] = dict(source=str(stage / record["path"]), path=record["path"],
                                 role="translated_title_source" if record["path"].endswith("-translated.zip") else "title_build_source")
items["recipes/precompiled-titles.json"] = dict(source=str(stage / "recipes/precompiled-titles.json"),
                                                path="recipes/precompiled-titles.json", role="precompiled_catalog")
layout = out / "layout.json"
atomic_write_json(layout, dict(format="triaevum_public_release_layout_v1", target=catalog["target"], forge_profile="rust", files=list(items.values())))
result = package_release(layout, package, source_root=repo, version=a.version, source_commit=a.commit,
                         enforce_readiness=not a.candidate)
atomic_write_json(out / "package-report.json", result)
print(json.dumps({k: result[k] for k in result if k != "files"} if isinstance(result, dict) else result, indent=1)[:4000])
