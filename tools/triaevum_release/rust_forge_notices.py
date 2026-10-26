"""Third-party notices for the Rust Forge, generated from the pinned Cargo.lock.

The shipped dependency graph is what `cargo tree` resolves for the published
products (TriAevumForge and triaevum-forge on Windows and Linux,
libtriaevum_forge.so on Android) with normal and build edges and the features
those packages actually enable; dev-only and other-target crates are listed as
not shipped. License texts come from each crate's published package (all its
LICENSE/COPYING/NOTICE files); a crate that publishes none gets the canonical
text of its elected license. Publisher tooling: it reads the local Cargo
registry and the pinned toolchain, never the network.

    python tools/triaevum_release/rust_forge_notices.py          # regenerate
    python tools/triaevum_release/rust_forge_notices.py --check  # verify
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FORGE_ROOT = REPO_ROOT / "tools/triaevum_forge"
INVENTORY = FORGE_ROOT / "THIRD_PARTY_CRATES.json"
NOTICES = REPO_ROOT / "LICENSES/Rust-Forge-crates.md"
NOTICES_RELATIVE = "LICENSES/Rust-Forge-crates.md"
FORMAT = "triaevum_rust_third_party_v1"

# Each published product: its target and the workspace packages it is built from.
PRODUCTS = {
    "windows": ("x86_64-pc-windows-msvc", ("forge-desktop", "forge-cli")),
    "linux": ("x86_64-unknown-linux-gnu", ("forge-desktop", "forge-cli")),
    "android": ("aarch64-linux-android", ("forge-android",)),
}

# Licenses known to be compatible with distributing the Forge under
# GPL-3.0-or-later when their notices are kept. Preference order elects one
# side of a choice ("MIT OR Apache-2.0" -> MIT).
ACCEPTED = ("MIT", "Apache-2.0", "Apache-2.0 WITH LLVM-exception", "BSD-3-Clause", "BSD-2-Clause",
            "ISC", "Zlib", "0BSD", "Unlicense")
# Permissive licenses outside the plan's MIT/Apache/BSD/ISC/Zlib set: shipped
# with their notices, recorded here for an explicit project decision.
PENDING_REVIEW = ("BSL-1.0", "Unicode-3.0")

LICENSE_FILE = re.compile(r"^(licen[cs]e|copying|copyright|notice|unlicense)", re.IGNORECASE)
EMAIL = re.compile(r"\s*<[^>]*@[^>]*>")
# Canonical texts for crates that publish no license file: the pinned
# toolchain's copies (share/doc/rust/licenses), else another shipped crate's
# file whose whole license is that identifier.
TOOLCHAIN_LICENSES = {"MIT": "MIT.txt", "Apache-2.0": "Apache-2.0.txt", "ISC": "ISC.txt",
                      "BSD-2-Clause": "BSD-2-Clause.txt", "Unicode-3.0": "Unicode-3.0.txt"}


class NoticeError(ValueError):
    pass


# --- SPDX expressions -------------------------------------------------------

def _tokens(expression: str) -> list[str]:
    # Old crates write "MIT/Apache-2.0" for a choice.
    spaced = expression.replace("(", " ( ").replace(")", " ) ").replace("/", " OR ")
    return spaced.split()


def _parse(tokens: list[str], position: int = 0):
    """Disjunctive normal form: a list of alternatives, each a sorted tuple of licenses."""
    def term(index):
        if index >= len(tokens):
            raise NoticeError("incomplete license expression")
        if tokens[index] == "(":
            value, index = expression(index + 1)
            if index >= len(tokens) or tokens[index] != ")":
                raise NoticeError("unbalanced license expression")
            return value, index + 1
        name = tokens[index]
        index += 1
        if index < len(tokens) and tokens[index] == "WITH":
            name, index = f"{name} WITH {tokens[index + 1]}", index + 2
        return [(name,)], index

    def conjunction(index):
        value, index = term(index)
        while index < len(tokens) and tokens[index] == "AND":
            right, index = term(index + 1)
            value = [tuple(sorted(set(a + b))) for a in value for b in right]
        return value, index

    def expression(index):
        value, index = conjunction(index)
        while index < len(tokens) and tokens[index] == "OR":
            right, index = conjunction(index + 1)
            value = value + right
        return value, index

    value, position = expression(position)
    if position != len(tokens):
        raise NoticeError("trailing tokens in license expression")
    return value


def elect(expression: str) -> tuple[str, ...]:
    """The licenses Forge distributes a crate under, or NoticeError when no
    alternative is wholly accepted or pending review."""
    if not expression:
        raise NoticeError("crate declares no license")
    known = ACCEPTED + PENDING_REVIEW
    rank = {name: index for index, name in enumerate(known)}
    choices = [alternative for alternative in _parse(_tokens(expression))
               if all(name in rank for name in alternative)]
    if not choices:
        raise NoticeError(f"no accepted alternative in {expression!r}")
    return min(choices, key=lambda alternative: (any(n in PENDING_REVIEW for n in alternative),
                                                 len(alternative), [rank[n] for n in alternative]))


# --- Cargo -----------------------------------------------------------------

def _cargo(arguments: list[str], cargo: str, offline: bool) -> str:
    command = [cargo, *arguments, "--locked"] + (["--offline"] if offline else [])
    completed = subprocess.run(command, cwd=FORGE_ROOT, capture_output=True, text=True, encoding="utf-8")
    if completed.returncode != 0:
        raise NoticeError(f"{' '.join(command[:3])} failed: {completed.stderr.strip()[-2000:]}")
    return completed.stdout


def shipped_packages(cargo: str = "cargo", offline: bool = True) -> dict[str, set[tuple[str, str]]]:
    shipped = {}
    for product, (target, packages) in PRODUCTS.items():
        selection = [argument for package in packages for argument in ("-p", package)]
        output = _cargo(["tree", "-e", "normal,build", "--target", target, *selection,
                         "--prefix", "none", "-f", "{p}"], cargo, offline)
        found = set()
        for line in output.splitlines():
            match = re.match(r"^(\S+) v(\S+)", line.strip())
            if match:
                found.add((match[1], match[2]))
        shipped[product] = found
    return shipped


def lock_packages(lock: Path = FORGE_ROOT / "Cargo.lock") -> set[tuple[str, str]]:
    """Registry/git packages of Cargo.lock (workspace members have no source)."""
    document = tomllib.loads(lock.read_text(encoding="utf-8"))
    return {(item["name"], item["version"]) for item in document.get("package", []) if "source" in item}


def _normalized(text: str) -> str:
    lines = [line.rstrip() for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n")]
    while lines and not lines[0]:
        lines.pop(0)
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines) + "\n"


def _license_files(directory: Path) -> list[tuple[str, str]]:
    files = []
    for entry in sorted(directory.iterdir(), key=lambda path: path.name):
        if not LICENSE_FILE.match(entry.name):
            continue
        children = sorted(entry.iterdir(), key=lambda path: path.name) if entry.is_dir() else [entry]
        for child in children:
            if child.is_file():
                relative = child.relative_to(directory).as_posix()
                files.append((relative, _normalized(child.read_bytes().decode("utf-8", errors="replace"))))
    return files


def _toolchain_licenses(cargo: str) -> Path:
    # The rustc beside the given cargo; rustup resolves rust-toolchain.toml in FORGE_ROOT.
    rustc = Path(cargo).with_name("rustc" + Path(cargo).suffix) if Path(cargo).parent != Path() else "rustc"
    sysroot = subprocess.run([str(rustc), "--print", "sysroot"], cwd=FORGE_ROOT, capture_output=True,
                             text=True, check=True).stdout.strip()
    return Path(sysroot) / "share/doc/rust/licenses"


def toolchain_channel() -> str:
    return tomllib.loads((FORGE_ROOT / "rust-toolchain.toml").read_text(encoding="utf-8"))["toolchain"]["channel"]


def collect(cargo: str = "cargo", offline: bool = True) -> tuple[dict, str]:
    metadata = json.loads(_cargo(["metadata", "--format-version", "1"], cargo, offline))
    members = set(metadata["workspace_members"])
    packages = {(item["name"], item["version"]): item for item in metadata["packages"] if item["id"] not in members}
    shipped = shipped_packages(cargo, offline)
    union = set().union(*shipped.values()) & set(packages)
    texts: dict[str, str] = {}
    crates = []
    canonical_sources: dict[str, str] = {}
    pending_fallback = []
    for key in sorted(union):
        item = packages[key]
        expression = item.get("license") or ""
        elected = elect(expression)
        files = _license_files(Path(item["manifest_path"]).parent)
        record = {
            "name": key[0], "version": key[1], "license": expression, "elected": list(elected),
            "products": sorted(product for product, found in shipped.items() if key in found),
            "repository": item.get("repository") or "",
            "authors": [EMAIL.sub("", author).strip() for author in item.get("authors", [])],
            "texts": [],
        }
        for relative, text in files:
            digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
            texts.setdefault(digest, text)
            record["texts"].append({"file": relative, "sha256": digest})
        if len(elected) == 1 and expression.strip() == elected[0] and files:
            canonical_sources.setdefault(elected[0], record["texts"][0]["sha256"])
        if not files:
            pending_fallback.append(record)
        crates.append(record)
    toolchain = _toolchain_licenses(cargo)
    for record in pending_fallback:
        for name in record["elected"]:
            if name in TOOLCHAIN_LICENSES and (toolchain / TOOLCHAIN_LICENSES[name]).is_file():
                text = _normalized((toolchain / TOOLCHAIN_LICENSES[name]).read_text(encoding="utf-8"))
                holders = ", ".join(record["authors"]) or f"the {record['name']} authors"
                text = text.replace("<year> <copyright holders>", holders)
                source = f"canonical {name} (crate publishes no license file)"
            elif name in canonical_sources:
                text = texts[canonical_sources[name]]
                source = f"canonical {name} (crate publishes no license file)"
            else:
                raise NoticeError(f"{record['name']} {record['version']} publishes no {name} text")
            digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
            texts.setdefault(digest, text)
            record["texts"].append({"file": source, "sha256": digest})
    unshipped = sorted(lock_packages() - union)
    inventory = {
        "format": FORMAT,
        "generator": "tools/triaevum_release/rust_forge_notices.py",
        "toolchain": toolchain_channel(),
        "products": {product: {"target": target, "packages": list(packages_)}
                     for product, (target, packages_) in PRODUCTS.items()},
        "notices": NOTICES_RELATIVE,
        "accepted_licenses": list(ACCEPTED),
        "pending_review_licenses": list(PENDING_REVIEW),
        "crates": crates,
        "not_shipped": [{"name": name, "version": version} for name, version in unshipped],
    }
    return inventory, render(inventory, texts)


def inventory_bytes(inventory: dict) -> bytes:
    return (json.dumps(inventory, indent=2) + "\n").encode("utf-8")


def render(inventory: dict, texts: dict[str, str]) -> str:
    digest = hashlib.sha256(inventory_bytes(inventory)).hexdigest()
    order = []
    for record in inventory["crates"]:
        for item in record["texts"]:
            if item["sha256"] not in order:
                order.append(item["sha256"])
    number = {sha: index + 1 for index, sha in enumerate(order)}
    lines = [
        "# TriAevum Forge (Rust): third-party crates",
        "",
        "Generated by `tools/triaevum_release/rust_forge_notices.py` from `tools/triaevum_forge/Cargo.lock`;",
        "do not edit by hand. Machine-readable inventory: `tools/triaevum_forge/THIRD_PARTY_CRATES.json`",
        f"(SHA-256 `{digest}`).",
        "",
        "The Rust Forge (`TriAevumForge`, `triaevum-forge` and Android's `libtriaevum_forge.so`) is",
        "licensed GPL-3.0-or-later. It statically links the crates below, each under the license",
        "elected from its declared expression, and the Rust standard library of toolchain",
        f"{inventory['toolchain']} (MIT OR Apache-2.0; its bundled components are listed in the Rust",
        "distribution's `COPYRIGHT-library.html`). Products: windows = x86_64-pc-windows-msvc,",
        "linux = x86_64-unknown-linux-gnu, android = aarch64-linux-android.",
        "",
        "| Crate | Version | Declared license | Elected | Products | License texts |",
        "|---|---|---|---|---|---|",
    ]
    for record in inventory["crates"]:
        refs = ", ".join(f"[T{number[item['sha256']]}](#t{number[item['sha256']]})" for item in record["texts"])
        lines.append(f"| `{record['name']}` | {record['version']} | {record['license']} | "
                     f"{' AND '.join(record['elected'])} | {', '.join(record['products'])} | {refs} |")
    lines += ["", "## License texts", ""]
    users: dict[str, list[str]] = {sha: [] for sha in order}
    files: dict[str, set[str]] = {sha: set() for sha in order}
    for record in inventory["crates"]:
        for item in record["texts"]:
            label = f"{record['name']} {record['version']}"
            if label not in users[item["sha256"]]:
                users[item["sha256"]].append(label)
            files[item["sha256"]].add(item["file"])
    for sha in order:
        text = texts[sha]
        fence = "`" * max(4, 1 + max((len(run) for run in re.findall(r"`+", text)), default=0))
        lines += [f'<a id="t{number[sha]}"></a>', f"### T{number[sha]}", "",
                  f"Files: {', '.join(sorted(files[sha]))}. Used by: {', '.join(users[sha])}.", "",
                  fence + "text", text.rstrip("\n"), fence, ""]
    return "\n".join(lines)


# --- Checks ----------------------------------------------------------------

def inventory_errors(inventory: dict, lock: set[tuple[str, str]], notices_text: str,
                     inventory_digest: str) -> list[str]:
    """Cheap consistency checks that need neither cargo nor the registry."""
    errors = []
    if inventory.get("format") != FORMAT:
        errors.append("unsupported third-party inventory format")
    shipped = {(item["name"], item["version"]) for item in inventory.get("crates", [])}
    listed = shipped | {(item["name"], item["version"]) for item in inventory.get("not_shipped", [])}
    for name, version in sorted(lock - listed):
        errors.append(f"Cargo.lock crate without a notice decision: {name} {version} "
                      "(run tools/triaevum_release/rust_forge_notices.py)")
    for name, version in sorted(listed - lock):
        errors.append(f"notice inventory names a crate absent from Cargo.lock: {name} {version}")
    if f"(SHA-256 `{inventory_digest}`)" not in notices_text:
        errors.append(f"{NOTICES_RELATIVE} was not generated from the current inventory")
    for record in inventory.get("crates", []):
        label = f"{record.get('name')} {record.get('version')}"
        try:
            if list(elect(record.get("license", ""))) != record.get("elected"):
                errors.append(f"{label}: elected license differs from its declared expression")
        except NoticeError as exc:
            errors.append(f"{label}: {exc}")
        if not record.get("texts"):
            errors.append(f"{label}: no license text")
        if f"| `{record.get('name')}` | {record.get('version')} |" not in notices_text:
            errors.append(f"{label}: missing from {NOTICES_RELATIVE}")
    return errors


def check_checked_in() -> list[str]:
    raw = INVENTORY.read_bytes()
    return inventory_errors(json.loads(raw), lock_packages(), NOTICES.read_text(encoding="utf-8"),
                            hashlib.sha256(raw).hexdigest())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="regenerate in memory and compare with the checked-in files")
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--online", action="store_true", help="let cargo refresh its registry index")
    args = parser.parse_args(argv)
    try:
        inventory, notices = collect(args.cargo, offline=not args.online)
    except (NoticeError, OSError, subprocess.CalledProcessError) as exc:
        print(f"Rust notices failed: {exc}", file=sys.stderr)
        return 1
    expected = {INVENTORY: inventory_bytes(inventory), NOTICES: notices.encode("utf-8")}
    if args.check:
        stale = [path.relative_to(REPO_ROOT).as_posix() for path, payload in expected.items()
                 if not path.is_file() or path.read_bytes() != payload]
        if stale:
            print("Out of date (regenerate with rust_forge_notices.py): " + ", ".join(stale), file=sys.stderr)
            return 1
        print(f"Rust notices current: {len(inventory['crates'])} shipped crates, "
              f"{len(inventory['not_shipped'])} not shipped")
        return 0
    for path, payload in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    pending = sorted({name for record in inventory["crates"] for name in record["elected"] if name in PENDING_REVIEW})
    print(json.dumps({"crates": len(inventory["crates"]), "not_shipped": len(inventory["not_shipped"]),
                      "pending_review_licenses": pending}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
