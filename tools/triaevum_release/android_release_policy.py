"""Android (arm64 APK) release contract and APK audit (owner directive 2026-11-09 12:20).

This module is the Android adapter's policy. It is separate from
platform_policy.resolve_policy on purpose: the Windows/Linux/macOS contracts
and their audit output are frozen, so Android is accepted only through these
explicit Android entry points and never through the desktop policy path
(platform_policy still rejects aarch64-linux-android).

Title facts live in titles/<title>/android.json (application id, label,
version, Gradle flavor); the APK audit refuses the other title's content.
"""

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import zipfile

try:
    from .release_platform import ANDROID, TITLES_ROOT
except ImportError:
    from release_platform import ANDROID, TITLES_ROOT


FACTS = "android.json"
ABI_DIR = "lib/arm64-v8a/"
PAGE_SIZE = 0x4000  # 16 KiB pages (FORKS_REPORT 2026-11-01 row 2)
REQUIRED_LIBRARIES = (
    "libSDL2.so", "libTriAevum.so", "libtriaevum_title_bootstrap.so", "libtriaevum_title_aot.so",
    "libtriaevum_forge.so", "libc++_shared.so",
)
FORGE_CATALOG = "assets/forge-package/precompiled-titles.json"
FORGE_INVENTORY = "assets/forge-package/forge-package.json"
# The publisher signature forge-android verifies before Forge reads a package file (A5).
FORGE_SIGNATURE = "assets/forge-package/release-signature.json"
# The APK carries no game data and no developer artefact: the Android Forge
# produces game content on the device from the owner's own dump.
FORBIDDEN_EXTENSIONS = (
    ".gar", ".mfpj", ".3ds", ".cci", ".cia", ".cxi", ".cfa", ".app", ".romfs", ".bcsar", ".bcstm",
    ".zar", ".pdb", ".debug", ".dwo", ".map", ".jks", ".keystore", ".p12", ".pem",
)
FORBIDDEN_BASENAMES = ("exefs.bin", "romfs.bin", "code.bin", "gradle.properties", "local.properties")
FORBIDDEN_COMPONENTS = ("romfs", "exefs", ".git", "intermediates", "__pycache__")
TITLES = ("oot3d", "mm3d")
_VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?$")
_STAGES = {"alpha": 1, "beta": 2, "rc": 3, None: 9}
_APPLICATION_ID = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$")


def version_code(version_name: str) -> int:
    """M*10^7 + m*10^5 + p*10^3 + stage*100 + n (alpha 1, beta 2, rc 3, final 9)."""
    match = _VERSION.match(version_name or "")
    if not match:
        raise ValueError(f"Unsupported Android version name: {version_name!r}")
    major, minor, patch, stage, number = match.groups()
    major, minor, patch, number = int(major), int(minor), int(patch), int(number or 0)
    if minor > 99 or patch > 99 or number > 99 or major > 200:
        raise ValueError(f"Version out of the versionCode range: {version_name}")
    return major * 10**7 + minor * 10**5 + patch * 10**3 + _STAGES[stage] * 100 + number


def android_facts(title: str, titles_root: Path = TITLES_ROOT) -> dict:
    """The validated Android product facts of `title` (fails closed)."""
    if title not in TITLES:
        raise ValueError(f"Unknown title: {title!r}")
    facts = json.loads((titles_root / title / FACTS).read_text(encoding="utf-8"))
    product = json.loads((titles_root / title / "product.json").read_text(encoding="utf-8"))
    for key in ("title", "application_id", "label", "version_name", "flavor"):
        if not isinstance(facts.get(key), str) or not facts[key]:
            raise ValueError(f"Invalid Android facts for {title}: {key}")
    if facts["title"] != title or facts["label"] != product.get("product_name"):
        raise ValueError(f"Android facts of {title} disagree with its product facts")
    if not _APPLICATION_ID.match(facts["application_id"]):
        raise ValueError(f"Invalid application id for {title}")
    # Cargo features of libtriaevum_forge.so (crates/forge-android) for this title.
    features = facts.get("forge_features")
    if not isinstance(features, list) or not all(
            isinstance(item, str) and item.startswith("forge-android/") for item in features):
        raise ValueError(f"Invalid Android facts for {title}: forge_features")
    result = dict(facts)
    result["version_code"] = version_code(facts["version_name"])
    return result


def elf_errors(name: str, data: bytes) -> list[str]:
    """AArch64 shared object, every LOAD 16 KiB aligned, no debug sections."""
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        return [f"{name}: not an ELF64 little-endian file"]
    errors = []
    kind, machine = struct.unpack_from("<HH", data, 16)
    if kind != 3 or machine != 183:
        errors.append(f"{name}: not an AArch64 shared library")
    phoff, shoff = struct.unpack_from("<QQ", data, 32)
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", data, 54)
    loads = 0
    for index in range(phnum):
        offset = phoff + index * phentsize
        if offset + 56 > len(data):
            return errors + [f"{name}: truncated program headers"]
        p_type = struct.unpack_from("<I", data, offset)[0]
        p_align = struct.unpack_from("<Q", data, offset + 48)[0]
        if p_type == 1:
            loads += 1
            if p_align < PAGE_SIZE or p_align % PAGE_SIZE:
                errors.append(f"{name}: LOAD segment aligned to {p_align:#x}, needs {PAGE_SIZE:#x}")
    if loads == 0:
        errors.append(f"{name}: no LOAD segment")
    if shnum and shstrndx < shnum and shoff + shnum * shentsize <= len(data):
        base = shoff + shstrndx * shentsize
        str_off, str_size = struct.unpack_from("<QQ", data, base + 24)
        strings = data[str_off:str_off + str_size]
        for index in range(shnum):
            name_off = struct.unpack_from("<I", data, shoff + index * shentsize)[0]
            end = strings.find(b"\0", name_off)
            section = strings[name_off:end if end >= 0 else len(strings)]
            if section.startswith(b".debug_") or section == b".symtab":
                errors.append(f"{name}: unstripped ({section.decode('ascii', 'replace')})")
                break
    return errors


def parse_badging(text: str) -> dict:
    """The fields the audit needs from `aapt2 dump badging`."""
    facts = {"debuggable": "application-debuggable" in text}
    package = re.search(r"^package: name='([^']*)' versionCode='([^']*)' versionName='([^']*)'", text, re.M)
    if package:
        facts.update(application_id=package[1], version_code=int(package[2]), version_name=package[3])
    label = re.search(r"^application-label:'([^']*)'", text, re.M)
    if label:
        facts["label"] = label[1]
    return facts


def _patterns(markers) -> list:
    return [re.compile(str(marker).encode("ascii")) for marker in markers]


def _catalog_errors(catalog: dict, title: str, other, other_labels) -> list[str]:
    errors = []
    text = json.dumps(catalog)
    if catalog.get("title", title) != title:
        errors.append(f"Forge catalog title {catalog.get('title')!r} is not {title}")
    for item, label in zip(other, other_labels):
        if f'"{item}"' in text or label in text:
            errors.append(f"Forge catalog names {item}")
    runtime = catalog.get("runtime")
    path = runtime.get("path") if isinstance(runtime, dict) else None
    if path != ANDROID.runtime:
        errors.append(f"Forge catalog runtime {path!r} is not {ANDROID.runtime}")
    return errors


def audit_apk(apk: Path, title: str, *, badging: str | None, signer_sha256: str | None = None,
              expected_signer: str | None = None, privacy_markers=(), titles_root: Path = TITLES_ROOT,
              allow_unsigned: bool = False) -> list[str]:
    """Every reason the APK is not a release of `title` (empty = pass)."""
    facts = android_facts(title, titles_root)
    other = [item for item in TITLES if item != title]
    other_labels = [json.loads((titles_root / item / "product.json").read_text(encoding="utf-8"))["product_name"]
                    for item in other]
    patterns = _patterns(privacy_markers)
    errors: list[str] = []
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        libraries = {name[len(ABI_DIR):] for name in names if name.startswith(ABI_DIR)}
        for missing in sorted(set(REQUIRED_LIBRARIES) - libraries):
            errors.append(f"missing {ABI_DIR}{missing}")
        for name in names:
            path = PurePosixPath(name)
            lower = path.name.lower()
            if name.startswith("lib/") and not name.startswith(ABI_DIR):
                errors.append(f"{name}: only arm64-v8a libraries are released")
            if lower.endswith(FORBIDDEN_EXTENSIONS):
                errors.append(f"{name}: forbidden extension")
            if lower in FORBIDDEN_BASENAMES:
                errors.append(f"{name}: forbidden file")
            if any(part.lower() in FORBIDDEN_COMPONENTS for part in path.parts[:-1]):
                errors.append(f"{name}: forbidden path component")
            for item in other:
                if f"titles/{item}/" in name:
                    errors.append(f"{name}: belongs to {item}, not {title}")
            data = archive.read(name)
            if name.startswith(ABI_DIR) and name.endswith(".so"):
                errors.extend(elf_errors(name, data))
            if any(pattern.search(data) for pattern in patterns):
                errors.append(f"{name}: contains a build-machine path")
        if FORGE_CATALOG in names:
            errors.extend(_catalog_errors(json.loads(archive.read(FORGE_CATALOG)), title, other, other_labels))
        else:
            errors.append(f"missing {FORGE_CATALOG}")
        if FORGE_INVENTORY not in names:
            errors.append(f"missing {FORGE_INVENTORY}")
        if FORGE_SIGNATURE not in names:
            errors.append(f"missing {FORGE_SIGNATURE} (android_forge_package.py --sign)")
        signed = signer_sha256 is not None
        if not signed and not allow_unsigned:
            errors.append("APK signer not verified (apksigner verify --print-certs)")
    if badging is None:
        errors.append("no manifest facts (aapt2 dump badging) were provided")
    else:
        manifest = parse_badging(badging)
        if manifest["debuggable"]:
            errors.append("APK is debuggable")
        for key in ("application_id", "version_code", "version_name", "label"):
            if manifest.get(key) != facts[key]:
                errors.append(f"manifest {key} {manifest.get(key)!r} differs from {facts[key]!r}")
    if expected_signer is not None and (signer_sha256 or "").lower() != expected_signer.lower():
        errors.append("APK signer certificate differs from the release certificate")
    return errors


def privacy_sweep(root: Path, markers) -> list[str]:
    """Tracked-tree files under `root` (e.g. ports/android) that hold a private path."""
    patterns = _patterns(markers)
    hits = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if path.is_file() and not {".git", "build", ".gradle", ".cxx"} & set(relative.parts):
            data = path.read_bytes()
            if any(pattern.search(data) for pattern in patterns):
                hits.append(relative.as_posix())
    return hits


def _markers() -> list:
    try:
        from .audit_release import DEFAULT_POLICY
    except ImportError:
        from audit_release import DEFAULT_POLICY
    policy = json.loads(Path(DEFAULT_POLICY).read_text(encoding="utf-8"))
    return policy.get("forge_profiles", {}).get("rust", {}).get("binary_host_path_markers", [])


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    facts = sub.add_parser("facts")
    facts.add_argument("title", choices=TITLES)
    sweep = sub.add_parser("privacy-sweep")
    sweep.add_argument("root", type=Path)
    audit = sub.add_parser("audit")
    audit.add_argument("apk", type=Path)
    audit.add_argument("--title", required=True, choices=TITLES)
    audit.add_argument("--badging", type=Path, required=True)
    audit.add_argument("--signer-sha256")
    audit.add_argument("--expected-signer")
    audit.add_argument("--allow-unsigned", action="store_true", help="audit an unsigned release candidate")
    args = parser.parse_args(argv)
    if args.command == "facts":
        print(json.dumps(android_facts(args.title), indent=2, sort_keys=True))
        return 0
    if args.command == "privacy-sweep":
        hits = privacy_sweep(args.root, _markers())
        for hit in hits:
            print(f"error: private path in {hit}", file=sys.stderr)
        print(f"Android privacy sweep: {len(hits)} hit(s)")
        return 1 if hits else 0
    errors = audit_apk(args.apk, args.title, badging=args.badging.read_text(encoding="utf-8"),
                       signer_sha256=args.signer_sha256, expected_signer=args.expected_signer,
                       privacy_markers=_markers(), allow_unsigned=args.allow_unsigned)
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    print("Android APK audit: " + ("FAILED" if errors else "passed"))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
