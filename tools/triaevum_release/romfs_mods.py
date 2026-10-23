"""Optional RomFS mods, obtained and applied on the user's machine only.

A RomFS mod replaces native files of the user's own RomFS. TriAevum never
distributes a mod's files. On the user's request Forge obtains the author's
original, unmodified archive (from the mod page's download, or, when that fails,
from TriAevum's verbatim mirror once the maintainer has approved it), keeps it
only after its size and SHA-256 match the revision recipe, extracts the
catalogued members with the system's libarchive (bsdtar) and verifies each one.
The user may instead select a folder extracted with any tool; its members are
verified the same way.

Preparation then builds an O3RO overlay over the user's RomFS service view (see
oot3d_native_a32_romfs_overlay.h): the replacement files are appended after the
view and their file records point at them. Base bytes never move, nothing is
converted, and the overlay stays in the private data folder.

The enabled mods are a Forge setting (config/romfs_mods.json); Forge's
"Optional mods" section is its only control and "Prepare and install" applies
it. With no mod enabled, preparation and the launch profile are unchanged.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import ssl
import struct
import subprocess
import sys
import tempfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Mapping, Protocol

try:
    from .common import atomic_write_json, load_json_object, sha256_file
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file


SETTINGS_FORMAT = "triaevum_romfs_mods_v1"
SETTINGS_NAME = "romfs_mods.json"
OVERLAY_NAME = "romfs_overlay.o3ro"
SOURCE_RECEIPT = "source.json"
# 1: O3RO v1 overlay, file-record payload patches and an appended tail.
IMPORT_VERSION = 1
INVALID = 0xFFFFFFFF
PAYLOAD_ALIGNMENT = 16
MAXIMUM_MEMBER_BYTES = 64 * 1024 * 1024
# Frozen Linux builds carry the SDK's OpenSSL, whose default CA path may not
# exist on the host distribution.
LINUX_CA_BUNDLES = (
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem",
    "/etc/ssl/cert.pem",
)


class CataloguedArchive(Protocol):
    """What the archive functions need of a catalogued mod or texture pack
    (ui_texture_pack.py shares them)."""
    archive_kind: str
    title: str
    version: str
    downloads: tuple[str, ...]


@dataclass(frozen=True)
class Credit:
    group: str
    name: str
    role: str
    profile: str


@dataclass(frozen=True)
class RomFsMod:
    """A catalogued mod: identity, credits, licence and the files it replaces.

    Archive members are RomFS paths (the archive root is the RomFS root)."""
    mod_id: str
    archive_kind: str
    title: str
    version: str
    author: str
    page: str
    downloads: tuple[str, ...]
    license_name: str
    license_url: str
    credits: tuple[Credit, ...]
    members: Mapping[str, tuple[int, str]]

    @property
    def attribution(self) -> str:
        return f"{self.title} {self.version} by {self.author} ({self.page}), {self.license_name}"


# --- Vanilla+ - Link Remodel And HD Item Models 1.9 (HugAMuffin) -------------
# GameBanana's file link for link_remodel_vanilla_plus_1_9.rar; it redirects to
# the file host.
VANILLA_PLUS_LINK_DOWNLOAD = "https://gamebanana.com/dl/1827260"
# The verbatim mirror of the same archive, used only when the GameBanana
# download fails. PENDING MAINTAINER APPROVAL: the mirror is a publication of
# the author's file (docs/TRIAEVUM_MOD_VANILLA_PLUS_LINK.md). Forge never uses
# it until VANILLA_PLUS_LINK_MIRROR_APPROVED is set to True.
VANILLA_PLUS_LINK_MIRROR_URL = (
    "https://github.com/coccofresco/triaevum-mod-mirror/releases/download/"
    "vanilla-plus-link-1.9/link_remodel_vanilla_plus_1_9.rar")
VANILLA_PLUS_LINK_MIRROR_APPROVED = False

VANILLA_PLUS_LINK = RomFsMod(
    mod_id="vanilla_plus_link",
    archive_kind="vanilla_plus_link_1_9_archive",
    title="Vanilla+ - Link Remodel And HD Item Models",
    version="1.9",
    author="HugAMuffin",
    page="https://gamebanana.com/mods/705890",
    downloads=(VANILLA_PLUS_LINK_DOWNLOAD,) + (
        (VANILLA_PLUS_LINK_MIRROR_URL,) if VANILLA_PLUS_LINK_MIRROR_APPROVED else ()),
    license_name="CC BY-NC-ND 4.0",
    license_url="https://creativecommons.org/licenses/by-nc-nd/4.0/",
    credits=(
        Credit("Key Authors", "HugAMuffin", "3D Artists", "https://gamebanana.com/members/1608504"),
        Credit("Testing", "Fado", "QA Lead", "https://gamebanana.com/members/1713801"),
        Credit("Testing", "M-1", "Hardware Testing", "https://gamebanana.com/members/1544353"),
        Credit("Testing", "seiunsky_1084", "Hardware Testing", "https://gamebanana.com/members/3964924"),
        Credit("Tool Creation", "Jameriquiah", "Code Wizard", "https://gamebanana.com/members/1664482"),
    ),
    members={
        "actor/zelda_ganon2.zar": (1259956, "eac32e62c7ba4a0c1e7388c1e09acdc9dc9c33e2b0d2ec33a5d4f40a372f1c5a"),
        "actor/zelda_gi_bomb_1.zar": (51164, "00dce5c0098ec7e7b0afca8a5b7d3218c2df5ce3358aa8a77a4834fd8cb0b465"),
        "actor/zelda_gi_boomerang.zar": (25600, "f9e1b460d48299eb77a66527ad253394971b3603b081cb66ac8683bb59d2e43f"),
        "actor/zelda_gi_boots_2.zar": (33492, "d1a9b45256b5f7ae368d8b19ca51bb945625b1ed6e06bef0f589c4dd18e7b352"),
        "actor/zelda_gi_bosskey.zar": (22468, "40e76bcd742970b69dc68ae65718254cf37651eecab4fcef505d255a5a6de261"),
        "actor/zelda_gi_bow.zar": (23664, "096555f078a9cd8363e9a6afcf5ce0e6c183fb9de30e349dedee205cfc9d9a9e"),
        "actor/zelda_gi_bracelet.zar": (20152, "c2523166985391ce3b11e85340b9c2f734157e84deba972bc7a38c5ab9f2572a"),
        "actor/zelda_gi_brokensword.zar": (49124, "2a9dd18a5998726d5d7283c82d663c0842b04bd65e6ebfccbcdc4d6706155949"),
        "actor/zelda_gi_compass.zar": (33264, "7a97c05272f8356e7156525c078c63464f2c41beeef114204f31b39db2e7dc7c"),
        "actor/zelda_gi_glasses.zar": (60132, "26af56cabaa6a584d483ddf22f095fef4da97b601daedec1dd98a77f0613e6f6"),
        "actor/zelda_gi_hoverboots.zar": (38204, "0b64d1dd9f79e7b3c3d4320b2fde25e95a4c206ac81dfdb4725027510bba2992"),
        "actor/zelda_gi_key.zar": (13020, "781bb9256fb126747924c20d1460880a951b3af1f884f34bfe460ab33fb767ba"),
        "actor/zelda_gi_longsword.zar": (56272, "fb4a282c94bbdfcb889d7957aedfa5fad7294aa50ea0021a0fd1126fac90114e"),
        "actor/zelda_gi_shield_1.zar": (22516, "942706232fa5688c71d2392d0f770327aea122cfd2a2d2dd3c9b6cc2aa97bb96"),
        "actor/zelda_gi_shield_2.zar": (61296, "8ae7f3c600f82eee70a7e3176ec970416ae9ec413ce63a7fcb9712f3699a3aea"),
        "actor/zelda_gi_shield_3.zar": (53536, "94e8695a2efe8a367c1e36edde6f4f9c0b7efca098144ca0264622135c9a51e5"),
        "actor/zelda_gi_sutaru.zar": (26432, "0d99d7f42da1de634e6af5349a70e0a634121d6847ddc9584c61fddd8b1ffd77"),
        "actor/zelda_gi_sword_1.zar": (46816, "0612bd043f15c09b9cc7eac9c6a5b58244f627407c96327cadbfb130614049ca"),
        "actor/zelda_gi_sword_2.zar": (35492, "649ab6b1b03ca52ab2ae49cd4e988660a31eebae6e7c35b6bce03ce8bfc7b979"),
        "actor/zelda_keep.zar": (1672688, "ccca05f7f7b7d1b23e710cdb9a532aba1337fe15d8dfb9222f5178286c98f402"),
        "actor/zelda_link_boy_new.zar": (5837288, "0774d2f6d68531bbb52f0c9a197588f48bb9af10a894a0e160d1181c69c4b8dc"),
        "actor/zelda_link_child_new.zar": (5554268, "b82b5e9d10939bea5cbc8682b625232e80b4bda3484caafd0fffd872e59a2b17"),
        "actor/zelda_link_opening.zar": (1411188, "894815fc5fe6051edafa6046eb0febb52b6eda11f98a20c12bff73067745e3b8"),
        "actor/zelda_st.zar": (139072, "ac1a556609c150418e6aea0d77f5b3aa1430952d1dbcd723ea77e9cca2cd872d"),
        "actor/zelda_toki_objects.zar": (219232, "6dc40dc4fef7c2cad381bddfcea2c874599083dc81ccb70a091cf2b20f83fdd0"),
        "misc/menu_link.zar": (91108, "8b2d10be2fa6013cca8ec0053f76ac9573c3ba40c47a2319da6e6b38e40bcec3"),
    },
)

CATALOG: dict[str, RomFsMod] = {VANILLA_PLUS_LINK.mod_id: VANILLA_PLUS_LINK}


@dataclass(frozen=True)
class ArchiveContract:
    kind: str
    name: str
    bytes: int
    sha256: str


# --- Settings: the one owner is Forge's "Optional mods" section --------------

def settings_path(data_root: Path) -> Path:
    return data_root / "config" / SETTINGS_NAME


def enabled_mods(data_root: Path) -> tuple[str, ...]:
    """The enabled catalogued mods, in catalogue order. A missing file is none;
    a malformed file is an error, never a silent default."""
    path = settings_path(data_root)
    if not path.is_file():
        return ()
    payload = load_json_object(path)
    enabled = payload.get("enabled")
    if payload.get("format") != SETTINGS_FORMAT or not isinstance(enabled, list) or not all(
            isinstance(item, str) for item in enabled):
        raise ValueError(f"Invalid RomFS mod settings: {path}")
    return tuple(mod_id for mod_id in CATALOG if mod_id in enabled)


def set_mod_enabled(data_root: Path, mod_id: str, enabled: bool) -> tuple[str, ...]:
    if mod_id not in CATALOG:
        raise ValueError(f"Unknown RomFS mod: {mod_id}")
    current = set(enabled_mods(data_root))
    current = current | {mod_id} if enabled else current - {mod_id}
    ordered = [item for item in CATALOG if item in current]
    atomic_write_json(settings_path(data_root), {"format": SETTINGS_FORMAT, "enabled": ordered})
    return tuple(ordered)


# --- The original archive -------------------------------------------------------

def archive_contract(recipes: Path | dict, mod: CataloguedArchive) -> ArchiveContract | None:
    """The archive the recipes (a recipes file, or one recipe) accept for
    `mod`, or None."""
    payload = recipes if isinstance(recipes, dict) else load_json_object(recipes)
    items = payload["recipes"] if isinstance(payload.get("recipes"), list) else [payload]
    found: ArchiveContract | None = None
    for recipe in items:
        optional = recipe.get("optional_inputs") if isinstance(recipe, dict) else None
        item = optional.get(mod.archive_kind) if isinstance(optional, dict) else None
        if not isinstance(item, dict):
            continue
        contract = ArchiveContract(mod.archive_kind, str(item["name"]), int(item["bytes"]),
                                   str(item["sha256"]).lower())
        if found is not None and found != contract:
            raise ValueError(f"Recipes disagree about the {mod.title} archive")
        found = contract
    return found


def downloads_directory(data_root: Path) -> Path:
    return data_root / "downloads"


def _verified(path: Path, contract: ArchiveContract) -> bool:
    return (path.is_file() and not path.is_symlink() and path.stat().st_size == contract.bytes
            and sha256_file(path) == contract.sha256)


def stored_archive(root: Path, data_root: Path, contract: ArchiveContract) -> Path | None:
    """The original archive beside Forge, under mods/ beside it, or in
    data/downloads, by size only (cheap; extraction verifies the hash)."""
    for directory in (root, root / "mods", downloads_directory(data_root)):
        path = directory / contract.name
        if path.is_file() and path.stat().st_size == contract.bytes:
            return path
    return None


def _keep(temporary: Path, data_root: Path, contract: ArchiveContract) -> Path:
    destination = downloads_directory(data_root) / contract.name
    os.replace(temporary, destination)
    return destination


def import_archive(source: Path, data_root: Path, mod: CataloguedArchive, contract: ArchiveContract) -> Path:
    """Copy a user-selected archive into data/downloads after verification."""
    source = source.expanduser().resolve()
    if not source.is_file():
        raise ValueError(f"Not a file: {source}")
    if not _verified(source, contract):
        raise ValueError(f"This is not the original {mod.title} {mod.version} archive "
                         f"({contract.name}): {source.name}")
    directory = downloads_directory(data_root)
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / contract.name
    if destination.resolve() == source:
        return destination
    descriptor, name = tempfile.mkstemp(prefix=".", suffix=".part", dir=directory)
    os.close(descriptor)
    temporary = Path(name)
    try:
        shutil.copyfile(source, temporary)
        if not _verified(temporary, contract):
            raise ValueError("The copied archive changed while it was being copied")
        return _keep(temporary, data_root, contract)
    finally:
        temporary.unlink(missing_ok=True)


def tls_context() -> ssl.SSLContext:
    context = ssl.create_default_context()
    if sys.platform.startswith("linux") and not context.get_ca_certs():
        bundle = next((path for path in LINUX_CA_BUNDLES if os.path.isfile(path)), None)
        if bundle is not None:
            context.load_verify_locations(cafile=bundle)
    return context


def _download_once(url: str, data_root: Path, contract: ArchiveContract, mod: CataloguedArchive,
                   progress: Callable[[int, int], None], opener: Callable[..., object]) -> Path:
    directory = downloads_directory(data_root)
    directory.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=".", suffix=".part", dir=directory)
    temporary = Path(name)
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "TriAevumForge"})
        done = 0
        with os.fdopen(descriptor, "wb") as output, opener(request, timeout=60, context=tls_context()) as response:
            while chunk := response.read(1024 * 1024):
                done += len(chunk)
                if done > contract.bytes:
                    raise ValueError(f"the file is larger than {mod.title} {mod.version}: the source "
                                     "may offer another release")
                output.write(chunk)
                progress(done, contract.bytes)
        if not _verified(temporary, contract):
            raise ValueError(f"the file is not the {mod.title} {mod.version} archive")
        return _keep(temporary, data_root, contract)
    finally:
        temporary.unlink(missing_ok=True)


def download_archive(data_root: Path, mod: CataloguedArchive, contract: ArchiveContract, *,
                     urls: Iterable[str] | None = None,
                     progress: Callable[[int, int], None] = lambda _done, _total: None,
                     report: Callable[[str], None] = lambda _message: None,
                     opener: Callable[..., object] = urllib.request.urlopen) -> Path:
    """Download the original archive: the mod page's file first, then each
    approved mirror. Only a verified file is kept."""
    failures = []
    for url in (tuple(urls) if urls is not None else mod.downloads):
        try:
            return _download_once(url, data_root, contract, mod, progress, opener)
        except Exception as exc:  # Network, HTTP and verification failures alike.
            failures.append(f"{url}: {exc}")
            report(f"Download from {url} failed ({exc}); trying the next source...")
    raise ValueError("; ".join(failures) or "no download source is configured")


# --- Members: extraction and verification ---------------------------------------

def _bsdtar_candidates() -> list[str]:
    if os.name == "nt":
        # Windows 10 (1803) and later ship libarchive's bsdtar as tar.exe. Use
        # it by path: a GNU tar earlier on PATH cannot read RAR.
        system = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "tar.exe"
        return [str(system)] if system.is_file() else []
    return [path for path in (shutil.which("bsdtar"), shutil.which("tar")) if path]


def find_bsdtar() -> str | None:
    """A libarchive bsdtar, which reads RAR (including RAR5) archives."""
    for candidate in _bsdtar_candidates():
        try:
            result = subprocess.run([candidate, "--version"], capture_output=True, text=True, timeout=30,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        except (OSError, subprocess.SubprocessError):
            continue
        if result.returncode == 0 and "libarchive" in result.stdout:
            return candidate
    return None


def _member_ok(path: Path, expected: tuple[int, str]) -> bool:
    return (path.is_file() and not path.is_symlink() and path.stat().st_size == expected[0]
            and sha256_file(path) == expected[1])


def source_directory(data_root: Path, mod: RomFsMod) -> Path:
    return data_root / "mods" / "romfs-sources" / f"{mod.mod_id}-{mod.version}"


def stored_members(data_root: Path, mod: RomFsMod) -> dict[str, Path] | None:
    """The verified extracted members in the private data folder, or None."""
    directory = source_directory(data_root, mod)
    receipt = directory / SOURCE_RECEIPT
    if not receipt.is_file():
        return None
    try:
        payload = load_json_object(receipt)
    except (OSError, ValueError):
        return None
    if payload.get("mod") != mod.mod_id or payload.get("version") != mod.version:
        return None
    members = {name: directory / name for name in mod.members}
    if not all(_member_ok(path, mod.members[name]) for name, path in members.items()):
        return None
    return members


def _publish_members(staging: Path, data_root: Path, mod: RomFsMod, origin: str) -> dict[str, Path]:
    for name, expected in mod.members.items():
        if not _member_ok(staging / name, expected):
            raise ValueError(f"{mod.title}: {name} is missing or differs from version {mod.version}")
    atomic_write_json(staging / SOURCE_RECEIPT, {
        "mod": mod.mod_id, "version": mod.version, "origin": origin,
        "attribution": mod.attribution, "private_local_artifact": True})
    destination = source_directory(data_root, mod)
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    os.replace(staging, destination)
    members = stored_members(data_root, mod)
    if members is None:
        raise ValueError(f"{mod.title}: the extracted files changed while they were stored")
    return members


def extract_archive(archive: Path, data_root: Path, mod: RomFsMod, contract: ArchiveContract,
                    *, bsdtar: str | None = None) -> dict[str, Path]:
    """Extract exactly the catalogued members of the verified original archive."""
    if not _verified(archive, contract):
        raise ValueError(f"{mod.title} archive failed verification: {archive}")
    tool = bsdtar or find_bsdtar()
    if tool is None:
        raise ValueError("No RAR reader is available (libarchive's bsdtar). Extract the archive with any "
                         "tool and choose \"Select extracted folder...\".")
    parent = source_directory(data_root, mod).parent
    parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".extract-", dir=parent))
    try:
        # Named members only; bsdtar refuses absolute and ".." paths by default.
        result = subprocess.run([tool, "-x", "-f", str(archive), "-C", str(staging), *mod.members],
                                capture_output=True, text=True, timeout=600,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip().splitlines()[-1:] or ["unknown error"]
            raise ValueError(f"Cannot extract the {mod.title} archive: {detail[0]}. Extract it with "
                             "any tool and choose \"Select extracted folder...\".")
        return _publish_members(staging, data_root, mod, f"archive:{contract.sha256}")
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def import_extracted(folder: Path, data_root: Path, mod: RomFsMod) -> dict[str, Path]:
    """Verify and keep members from a folder the user extracted with any tool:
    the archive root (actor/, misc/), or a folder whose romfs/ is that root."""
    folder = folder.expanduser().resolve()
    roots = [folder, folder / "romfs"]
    root = next((item for item in roots if all((item / name).is_file() for name in mod.members)), None)
    if root is None:
        raise ValueError(f"This folder does not contain the extracted {mod.title} files "
                         f"(expected {next(iter(mod.members))} and the others)")
    parent = source_directory(data_root, mod).parent
    parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".import-", dir=parent))
    try:
        for name, expected in mod.members.items():
            source = root / name
            if source.is_symlink() or not _member_ok(source, expected):
                raise ValueError(f"{name} differs from {mod.title} {mod.version}")
            (staging / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, staging / name)
        return _publish_members(staging, data_root, mod, "extracted_folder")
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def mod_status(root: Path, data_root: Path, mod: RomFsMod, contract: ArchiveContract | None) -> str:
    """'ready' (verified files stored), 'archive' (archive stored, not yet
    extracted) or 'missing'."""
    if stored_members(data_root, mod) is not None:
        return "ready"
    if contract is not None and stored_archive(root, data_root, contract) is not None:
        return "archive"
    return "missing"


# --- The overlay ----------------------------------------------------------------

def _align(value: int, alignment: int = PAYLOAD_ALIGNMENT) -> int:
    return (value + alignment - 1) // alignment * alignment


def romfs_file_records(stream, base: int, size: int) -> tuple[tuple[int, ...], dict[str, tuple[int, int, int]]]:
    """The Level-3 header and {path: (file record offset, payload offset,
    payload size)} of a RomFS service view, by directory traversal."""
    def read(offset: int, length: int) -> bytes:
        if offset < 0 or length < 0 or offset + length > size:
            raise ValueError("RomFS extent outside the service view")
        stream.seek(base + offset)
        data = stream.read(length)
        if len(data) != length:
            raise ValueError("Truncated RomFS")
        return data

    header = struct.unpack("<10I", read(0, 40))
    if header[0] != 40:
        raise ValueError("Invalid RomFS Level-3 header")
    for offset, length in ((header[1], header[2]), (header[3], header[4]),
                           (header[5], header[6]), (header[7], header[8])):
        if offset % 4 or not 0 < length <= 16 * 1024 * 1024 or offset + length > size:
            raise ValueError("Invalid RomFS metadata bounds")
    if header[9] > size:
        raise ValueError("Invalid RomFS data offset")
    directories, files = read(header[3], header[4]), read(header[7], header[8])

    def name_of(table: bytes, offset: int, fixed: int) -> str:
        if offset % 4 or offset + fixed > len(table):
            raise ValueError("Invalid RomFS record")
        length = struct.unpack_from("<I", table, offset + fixed - 4)[0]
        if length % 2 or offset + fixed + length > len(table):
            raise ValueError("Invalid RomFS name")
        name = table[offset + fixed:offset + fixed + length].decode("utf-16-le")
        if "/" in name or "\\" in name or "\0" in name or name in (".", ".."):
            raise ValueError("Invalid RomFS name")
        return name

    records: dict[str, tuple[int, int, int]] = {}
    visited_directories: set[int] = set()
    visited_files: set[int] = set()
    pending = [(0, "")]
    while pending:
        offset, prefix = pending.pop()
        if offset in visited_directories:
            raise ValueError("Cyclic RomFS directory")
        visited_directories.add(offset)
        name = name_of(directories, offset, 24)
        _parent, sibling, child, file, _chain, _ = struct.unpack_from("<6I", directories, offset)
        path = f"{prefix}/{name}" if prefix and name else (name or prefix)
        if sibling != INVALID:
            pending.append((sibling, prefix))
        if child != INVALID:
            pending.append((child, path))
        while file != INVALID:
            if file in visited_files:
                raise ValueError("Cyclic RomFS file")
            visited_files.add(file)
            text = name_of(files, file, 32)
            owner, following, payload, length, _chain, _ = struct.unpack_from("<IIQQII", files, file)
            if owner != offset or header[9] + payload + length > size:
                raise ValueError("Invalid RomFS file record")
            full = f"{path}/{text}" if path else text
            if full in records:
                raise ValueError("Duplicate RomFS path")
            records[full] = (file, payload, length)
            file = following
    return header, records


def build_romfs_overlay(romfs: Path, *, service_offset: int, service_size: int,
                        replacements: Mapping[str, Path], output: Path) -> dict:
    """Write an O3RO v1 overlay: each replaced file's record points at its new
    payload, appended (16-byte aligned) after the service view."""
    if not replacements:
        raise ValueError("A RomFS overlay needs at least one replaced file")
    with romfs.open("rb") as stream:
        if service_offset < 0 or service_size <= 0 or service_offset + service_size > romfs.stat().st_size:
            raise ValueError("The RomFS service view is outside its image")
        header, records = romfs_file_records(stream, service_offset, service_size)
        patches = []
        tail_layout = []
        cursor = service_size
        for path in sorted(replacements):
            if path not in records:
                raise ValueError(f"The RomFS has no {path} to replace")
            record, _payload, _length = records[path]
            source = replacements[path]
            length = source.stat().st_size
            if not 0 < length <= MAXIMUM_MEMBER_BYTES:
                raise ValueError(f"Invalid replacement size for {path}")
            cursor = _align(cursor)
            field = header[7] + record + 8
            stream.seek(service_offset + field)
            expected = stream.read(16)
            patches.append((field, expected, struct.pack("<QQ", cursor - header[9], length)))
            tail_layout.append((cursor, source, length))
            cursor += length
    patches.sort()
    view_size = cursor
    table_bytes = 64 + 32 * len(patches)
    data_offset = table_bytes
    tail_offset = table_bytes + sum(len(expected) + len(new) for _, expected, new in patches)
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=".", suffix=".tmp", dir=output.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(struct.pack("<4sIQQIIQ24x", b"O3RO", 1, service_size, view_size,
                                     len(patches), 0, tail_offset))
            for field, expected, new in patches:
                stream.write(struct.pack("<QIIQQ", field, len(expected), 0,
                                         data_offset, data_offset + len(expected)))
                data_offset += len(expected) + len(new)
            for _field, expected, new in patches:
                stream.write(expected)
                stream.write(new)
            position = service_size
            for view_offset, source, length in tail_layout:
                stream.write(bytes(view_offset - position))
                with source.open("rb") as payload:
                    shutil.copyfileobj(payload, stream, 4 * 1024 * 1024)
                position = view_offset + length
            stream.flush()
            os.fsync(stream.fileno())
        if temporary.stat().st_size != tail_offset + (view_size - service_size):
            raise ValueError("The RomFS overlay was not written completely")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return {"files": len(patches), "base_view_bytes": service_size, "appended_bytes": view_size - service_size}


def prepare_romfs_mods(*, root: Path, data_root: Path, recipe: dict, romfs: Path, romfs_sha256: str,
                       service_view: Callable[[], tuple[int, int]],
                       report: Callable[[str, str], None] = lambda *_: None) -> Path | None:
    """The overlay of the user's enabled RomFS mods, or None when none apply.

    `service_view` gives the runtime's (offset, size) RomFS view; it is read
    only when a mod applies. A mod that is enabled but unavailable is reported
    and left out: the title is still prepared, with the native files."""
    applied: list[tuple[RomFsMod, ArchiveContract, dict[str, Path]]] = []
    for mod_id in enabled_mods(data_root):
        mod = CATALOG[mod_id]
        contract = archive_contract(recipe, mod)
        if contract is None:
            report("mods", f"{mod.title} does not support this game revision; it is not applied.")
            continue
        members = stored_members(data_root, mod)
        if members is None:
            archive = stored_archive(root, data_root, contract)
            if archive is None:
                report("mods", f"{mod.title} is enabled but not downloaded; the native files are used.")
                continue
            report("mods", f"Extracting {mod.title} {mod.version} from the original archive...")
            try:
                members = extract_archive(archive, data_root, mod, contract)
            except (OSError, ValueError, subprocess.SubprocessError) as exc:
                report("mods", f"{exc} The native files are used.")
                continue
        applied.append((mod, contract, members))
    if not applied:
        return None
    service_offset, service_size = service_view()
    replacements: dict[str, Path] = {}
    for _mod, _contract, members in applied:
        replacements.update(members)  # Later catalogue entries win.
    identity = {"import_version": IMPORT_VERSION, "romfs_sha256": romfs_sha256,
                "service_offset": service_offset, "service_size": service_size,
                "mods": [[mod.mod_id, mod.version, contract.sha256] for mod, contract, _ in applied]}
    key = hashlib.sha256(repr(sorted(identity.items())).encode("ascii")).hexdigest()
    directory = data_root / "mods" / "romfs" / key
    overlay = directory / OVERLAY_NAME
    receipt_path = directory / "import.json"
    if overlay.is_file() and receipt_path.is_file():
        try:
            receipt = load_json_object(receipt_path)
            if receipt.get("source") == identity and receipt.get("overlay_sha256") == sha256_file(overlay):
                return overlay
        except (OSError, ValueError):
            pass
    report("mods", "Applying " + ", ".join(f"{mod.title} {mod.version}" for mod, _, _ in applied)
           + " to the local game data...")
    try:
        summary = build_romfs_overlay(romfs, service_offset=service_offset, service_size=service_size,
                                      replacements=replacements, output=overlay)
    except ValueError as exc:
        # E.g. a revision without one of the replaced files: keep the native files.
        report("mods", f"The enabled mods cannot be applied to this game data ({exc}); the native "
                       "files are used.")
        return None
    atomic_write_json(receipt_path, {
        "source": identity, "overlay_sha256": sha256_file(overlay), "summary": summary,
        "private_local_artifact": True, "redistributable": False,
        "mods": [{"id": mod.mod_id, "title": mod.title, "version": mod.version,
                  "attribution": mod.attribution, "license": mod.license_name,
                  "license_url": mod.license_url, "source_url": mod.page} for mod, _, _ in applied]})
    return overlay


def romfs_service_view(process_manifest: Path) -> tuple[int, int]:
    """(offset, size) of the RomFS service view the runtime exposes."""
    source = load_json_object(process_manifest).get("source", {})
    offset, size = source.get("romfs_service_offset"), source.get("romfs_service_size")
    if not isinstance(offset, int) or not isinstance(size, int) or offset < 0 or size <= 0:
        raise ValueError("The process manifest has no RomFS service view")
    return offset, size
