"""Install the SingleScreen assets TriAevum ships, never executable patches.

SingleScreen is part of TriAevum with its author's permission: its
low-resolution UI atlas (custom_ui01.ctxb) and Link animations
(zelda_link_ext.zar) from 3.0, and the HD Latin font (ltn16.qbf) from 3.1, are
in the package. The HD UI atlas (by chubbycubby) is not: it comes from the
user's prepared HD UI texture pack (ui_texture_pack.py), when the user has
obtained it.
"""

from __future__ import annotations

import hashlib
import shutil
import struct
import sys
from pathlib import Path
from typing import Callable

try:
    from .bundle_paths import distribution_root
    from .common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file
except ImportError:
    from bundle_paths import distribution_root
    from common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file

# The pack builders live under tools/oot3d; the release tools also run as plain
# modules from their own directory (the unittest suite), without the repository
# root on the path.
if str(distribution_root()) not in sys.path:
    sys.path.insert(0, str(distribution_root()))

from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import build_texture_pack  # noqa: E402
from tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack import ATTRIBUTION as FONT_ATTRIBUTION  # noqa: E402
from tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack import prepare_font_pack  # noqa: E402


# 2: SingleScreen 3.0 UI atlas, native localized labels (2.1.1 ones retired).
# 3: SingleScreen 3.0 HD UI atlas.
# 4: SingleScreen 3.0 is the only source; TopScreen 2.1.1 is retired.
# 5: the low-resolution assets come from the package.
# 6: the HD UI atlas comes from the HD UI texture pack.
IMPORT_VERSION = 6
# 3: the HD fonts come from SingleScreen 3.0.
# 4: the HD font comes from the package.
FONT_IMPORT_VERSION = 4
TITLE = "The Legend of Zelda: Ocarina of Time 3D"
UI_ATLAS = "custom_ui01.ctxb"
LINK_ANIMATIONS = "zelda_link_ext.zar"
HD_FONT = "ltn16.qbf"
# Package directories of the shipped assets, and their identity.
BUNDLED_DIRECTORY = Path("resources/topscreen/singlescreen-3.0")
BUNDLED_FONT_DIRECTORY = Path("resources/topscreen/singlescreen-3.1")
BUNDLED_ASSETS = {
    UI_ATLAS: (131144, "20290e417c56973cd19daa1f61a6c3963f075d61776db3d996954ee3296d07d8"),
    LINK_ANIMATIONS: (186404, "06d60e05c493e86677497eb935b49951d613249c43f896816ae898bb8b005902"),
}
BUNDLED_FONT = (427592, "8b83e081f721c6e1842b0a9cd90b71a3fb2849674d2b1040f3dc5c3221594169")
# The HD custom_ui01 of the texture pack's author, an emulator texture
# replacement. It also draws elements the native atlas lacks (SELECT/START,
# ZL/ZR labels). TriAevum uses it as the 3.0 atlas, keyed by its native content.
UI_ATLAS_HD = "ui_atlas_hd.png"
MOD_PAGE = "https://gamebanana.com/mods/695893"


def _bundled(root: Path, relative: Path, expected: tuple[int, str]) -> Path:
    path = root / relative
    if not path.is_file() or path.stat().st_size != expected[0] or sha256_file(path) != expected[1]:
        raise ValueError(f"The TriAevum package is incomplete or modified: {relative}. "
                         "Extract the release again.")
    return path


def bundled_assets(root: Path) -> dict[str, Path]:
    """The package's SingleScreen assets, verified."""
    assets = {name: _bundled(root, BUNDLED_DIRECTORY / name, expected)
              for name, expected in BUNDLED_ASSETS.items()}
    assets[HD_FONT] = _bundled(root, BUNDLED_FONT_DIRECTORY / HD_FONT, BUNDLED_FONT)
    return assets


def checked_ui_atlas_hd(path: Path) -> bytes:
    """The HD custom_ui01: a PNG with the native atlas's 2:1 aspect, data only."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError(f"The HD UI atlas is not a PNG: {path}")
    width, height = struct.unpack(">II", data[16:24])
    if width < 512 or width % 512 != 0 or width != height * 2:
        raise ValueError(f"The HD UI atlas has unexpected dimensions {width}x{height}")
    return data


def topscreen_ui_atlas_hd(pack: Path | None) -> Path | None:
    """The imported HD UI atlas beside a prepared texture pack."""
    if pack is None:
        return None
    candidate = pack.parent / UI_ATLAS_HD
    return candidate if candidate.is_file() else None


def topscreen_link_animations(pack: Path | None) -> Path | None:
    """The installed zelda_link_ext.zar beside a prepared texture pack."""
    if pack is None:
        return None
    candidate = pack.parent / LINK_ANIMATIONS
    return candidate if candidate.is_file() else None


def prepare_topscreen_assets(*, root: Path, data_root: Path, recipe: dict,
                             romfs: Path, ui_atlas_hd: Path | None = None,
                             report: Callable[[str, str], None] = lambda *_: None) -> Path | None:
    """The TopScreen texture overrides, with the Link animations, the HD font
    coverage and, from the prepared HD UI texture pack, the HD UI atlas beside
    them."""
    if recipe.get("title") != TITLE:
        return None  # Other titles do not inherit OOT3D UI assets.
    assets = bundled_assets(root)
    hd_atlas_sha256 = sha256_file(ui_atlas_hd) if ui_atlas_hd is not None else None
    identity = {"import_version": IMPORT_VERSION,
                "ui_atlas_sha256": BUNDLED_ASSETS[UI_ATLAS][1],
                "link_animations_sha256": BUNDLED_ASSETS[LINK_ANIMATIONS][1],
                "ui_atlas_hd_sha256": hd_atlas_sha256,
                "romfs_sha256": recipe["inputs"]["romfs"]["sha256"]}
    key = hashlib.sha256(repr(sorted(identity.items())).encode("ascii")).hexdigest()
    directory = data_root / "mods" / "topscreen" / key
    pack = directory / "atlas_overrides.o3tu"
    animations = directory / LINK_ANIMATIONS
    atlas_hd = directory / UI_ATLAS_HD
    font_pack = directory / "font_coverage.zip"
    font_receipt_path = directory / "fonts_import.json"
    receipt_path = directory / "import.json"
    textures_valid = False
    if pack.is_file() and receipt_path.is_file():
        receipt = load_json_object(receipt_path)
        textures_valid = (receipt.get("source") == identity and
                          receipt.get("pack_sha256") == sha256_file(pack) and
                          animations.is_file() and
                          sha256_file(animations) == BUNDLED_ASSETS[LINK_ANIMATIONS][1] and
                          (hd_atlas_sha256 is None or
                           (atlas_hd.is_file() and sha256_file(atlas_hd) == hd_atlas_sha256)))
    font_identity = {"font_import_version": FONT_IMPORT_VERSION, "font_sha256": BUNDLED_FONT[1],
                     "romfs_sha256": identity["romfs_sha256"]}
    fonts_valid = False
    if font_pack.is_file() and font_receipt_path.is_file():
        receipt = load_json_object(font_receipt_path)
        fonts_valid = (receipt.get("source") == font_identity and
                       receipt.get("pack_sha256") == sha256_file(font_pack))
    if textures_valid and fonts_valid:
        return pack

    directory.mkdir(parents=True, exist_ok=True)
    if not textures_valid:
        report("topscreen", "Installing the SingleScreen UI textures and Link animations...")
        atomic_write_bytes(pack, build_texture_pack(singlescreen_ui_atlas=assets[UI_ATLAS].read_bytes()))
        shutil.copyfile(assets[LINK_ANIMATIONS], animations)
        receipt = {"source": identity, "pack_sha256": sha256_file(pack),
                   "attribution": "TopScreen / Single Screen Experience by M-1",
                   "source_url": MOD_PAGE}
        if ui_atlas_hd is not None:
            report("topscreen", "Installing the HD UI atlas from the HD UI texture pack...")
            atomic_write_bytes(atlas_hd, checked_ui_atlas_hd(ui_atlas_hd))
            receipt["ui_atlas_hd_attribution"] = "chubbycubby, OoT3D Vanilla UI/HUD"
        else:
            atlas_hd.unlink(missing_ok=True)
        atomic_write_json(receipt_path, receipt)
    if not fonts_valid:
        report("topscreen", "Preparing the SingleScreen HD font...")
        manifest = prepare_font_pack(font=assets[HD_FONT], romfs=romfs, output=font_pack)
        atomic_write_json(font_receipt_path, {
            "source": font_identity, "pack_sha256": sha256_file(font_pack),
            "runtime_enabled": True,
            "minimum_output_height": 480,
            "status": "native_atlas_coverage_ready",
            "font_count": len(manifest["fonts"]),
            "attribution": FONT_ATTRIBUTION,
            "source_url": MOD_PAGE})
    return pack
