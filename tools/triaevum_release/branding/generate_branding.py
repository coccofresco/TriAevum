"""Derive every TriAevum branding asset from the checked-in masters.

    python -I tools/triaevum_release/branding/generate_branding.py
    python -I tools/triaevum_release/branding/generate_branding.py --import-sources DIR

The masters in `masters/` are the owner's artwork (README.md): the game icon,
the Forge icon and the Forge background. `--import-sources DIR` first rebuilds
the masters from the owner's full-size originals in DIR
(`game_icon_source.png`, `forge_icon_source.png`,
`forge_background_source.png`). Every other file is derived, so changing the
art means replacing a master and running this script; nothing is edited by
hand. Outputs (all checked in):

- `windows/triaevum.ico`, `windows/forge.ico`: the executables' icon resources
  (16 to 256 px; BMP frames below 256, PNG at 256);
- `forge/forge_window_icon.png`: the Forge window's icon (egui viewport);
- `linux/hicolor/<N>x<N>/apps/*.png`: the desktop entry's icons (AppImage,
  Flatpak) for the game and the Forge action;
- `tools/oot3d/native_demo_host/triaevum_window_icon_rgba.inc`: the game
  window's icon for X11 (SDL), 64x64 RGBA;
- `ports/android/app/src/main/res/mipmap-*`: the Android launcher icon
  (adaptive foreground and background, legacy square and round icons).

Requires Pillow. Output bytes are stable for one Pillow/zlib version.
"""

from __future__ import annotations

import argparse
import io
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

BRANDING = Path(__file__).resolve().parent
ROOT = BRANDING.parents[2]
MASTERS = BRANDING / "masters"
GAME_MASTER = MASTERS / "game_icon.png"
FORGE_MASTER = MASTERS / "forge_icon.png"
BACKGROUND_MASTER = MASTERS / "forge_background.jpg"

APP_ID = "io.github.coccofresco.TriAevum"
FORGE_ICON_NAME = f"{APP_ID}.Forge"
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
LINUX_SIZES = (64, 128, 256, 512)
MASTER_SIZE = 1024
WINDOW_ICON_SIZE = 64
FORGE_WINDOW_ICON_SIZE = 256
ANDROID_RES = ROOT / "ports/android/app/src/main/res"
# Launcher icon density buckets: (folder, legacy 48 dp icon, adaptive 108 dp layer) in pixels.
ANDROID_DENSITIES = (("mdpi", 48, 108), ("hdpi", 72, 162), ("xhdpi", 96, 216),
                     ("xxhdpi", 144, 324), ("xxxhdpi", 192, 432))
# Adaptive icons: the launcher masks a 108 dp layer to at least a 66 dp circle.
# The whole emblem, not only its ring, stays inside that circle with a margin.
ANDROID_SAFE_DIAMETER = 66.0 / 108.0
ANDROID_SAFE_MARGIN = 0.96
# The adaptive background: the game icon's deep teal, darker at the edges.
ANDROID_CENTER = (22, 64, 74)
ANDROID_EDGE = (6, 16, 24)
WINDOW_ICON_INC = ROOT / "tools/oot3d/native_demo_host/triaevum_window_icon_rgba.inc"
ALPHA_FLOOR = 12  # the originals' near-transparent haze around the emblem
BACKGROUND_QUALITY = 90


# --- masters ---------------------------------------------------------------

def _clean_alpha(image: Image.Image) -> Image.Image:
    red, green, blue, alpha = image.convert("RGBA").split()
    alpha = alpha.point(lambda value: 0 if value < ALPHA_FLOOR else value)
    return Image.merge("RGBA", (red, green, blue, alpha))


def _art_box(image: Image.Image) -> tuple[int, int, int, int]:
    box = image.getchannel("A").point(lambda value: 255 if value > 24 else 0).getbbox()
    if box is None:
        raise ValueError("the icon has no opaque pixels")
    return box


def _square_around_art(image: Image.Image, padding: float) -> Image.Image:
    """The emblem centred on a transparent square, `padding` of its side on each edge."""
    left, top, right, bottom = _art_box(image)
    side = max(right - left, bottom - top)
    canvas = round(side / (1.0 - 2.0 * padding))
    square = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    square.alpha_composite(image.crop((left, top, right, bottom)),
                           ((canvas - (right - left)) // 2, (canvas - (bottom - top)) // 2))
    return square


def import_sources(source: Path) -> None:
    MASTERS.mkdir(parents=True, exist_ok=True)
    for name, master in (("game_icon_source.png", GAME_MASTER), ("forge_icon_source.png", FORGE_MASTER)):
        image = _clean_alpha(Image.open(source / name))
        square = _square_around_art(image, 0.02).resize((MASTER_SIZE, MASTER_SIZE), Image.Resampling.LANCZOS)
        square.save(master, optimize=True)
    background = Image.open(source / "forge_background_source.png").convert("RGB")
    background.save(BACKGROUND_MASTER, quality=BACKGROUND_QUALITY, subsampling=0, optimize=True)


# --- sizes -----------------------------------------------------------------

def emblem(master: Image.Image, size: int, padding: float = 0.0) -> Image.Image:
    """The master at `size` px. Small sizes are sharpened a little: the art's
    fine engraving otherwise blurs into the ring."""
    inner = max(1, round(size * (1.0 - 2.0 * padding)))
    image = master.resize((inner, inner), Image.Resampling.LANCZOS)
    if size <= 48:
        rgb = image.convert("RGB").filter(ImageFilter.UnsharpMask(radius=0.6, percent=70, threshold=0))
        image = Image.merge("RGBA", (*rgb.split(), image.getchannel("A")))
    if inner == size:
        return image
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(image, ((size - inner) // 2, (size - inner) // 2))
    return canvas


def _png_bytes(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def _bmp_frame(image: Image.Image) -> bytes:
    """A 32-bit icon DIB: BITMAPINFOHEADER (double height), bottom-up BGRA, AND mask."""
    width, height = image.size
    pixels = image.convert("RGBA").tobytes()
    rows = []
    for y in range(height - 1, -1, -1):
        row = pixels[y * width * 4:(y + 1) * width * 4]
        rows.append(b"".join(bytes((row[x + 2], row[x + 1], row[x], row[x + 3])) for x in range(0, len(row), 4)))
    mask_stride = ((width + 31) // 32) * 4
    mask = []
    for y in range(height - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(width):
            if pixels[(y * width + x) * 4 + 3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask.append(bytes(bits))
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0,
                         len(b"".join(rows)) + len(b"".join(mask)), 0, 0, 0, 0)
    return header + b"".join(rows) + b"".join(mask)


def write_ico(path: Path, master: Image.Image, sizes=ICO_SIZES) -> None:
    frames = []
    for size in sizes:
        image = emblem(master, size, padding=0.0 if size <= 32 else 0.015)
        frames.append((size, _png_bytes(image) if size >= 256 else _bmp_frame(image)))
    offset = 6 + 16 * len(frames)
    directory = struct.pack("<HHH", 0, 1, len(frames))
    data = b""
    for size, frame in frames:
        dimension = 0 if size >= 256 else size
        directory += struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(frame), offset + len(data))
        data += frame
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(directory + data)


def write_png(path: Path, image: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(_png_bytes(image))


def write_window_icon_inc(path: Path, master: Image.Image) -> None:
    image = emblem(master, WINDOW_ICON_SIZE, padding=0.02)
    data = image.tobytes()
    lines = [
        "// Generated by tools/triaevum_release/branding/generate_branding.py from",
        "// masters/game_icon.png (artwork supplied by the project owner, see",
        "// tools/triaevum_release/branding/README.md). Do not edit.",
        f"// {WINDOW_ICON_SIZE}x{WINDOW_ICON_SIZE} RGBA, rows top to bottom, straight alpha.",
    ]
    for start in range(0, len(data), 16):
        lines.append(",".join(f"0x{byte:02x}" for byte in data[start:start + 16]) + ",")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


# --- Android ---------------------------------------------------------------

def _enclosing_radius(master: Image.Image) -> float:
    """Radius of the emblem's opaque pixels around the canvas centre, in master px."""
    alpha = master.getchannel("A")
    width, height = alpha.size
    centre_x, centre_y = (width - 1) / 2.0, (height - 1) / 2.0
    pixels = alpha.load()
    radius = 0.0
    for y in range(height):
        for x in range(width):
            if pixels[x, y] > 64:
                radius = max(radius, math.hypot(x - centre_x, y - centre_y))
    return radius


def _radial_background(size: int) -> Image.Image:
    image = Image.new("RGB", (size, size))
    pixels = image.load()
    centre = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            t = min(1.0, math.hypot(x - centre, y - centre) / (size * 0.62))
            t = t * t * (3.0 - 2.0 * t)
            pixels[x, y] = tuple(round(c + (e - c) * t) for c, e in zip(ANDROID_CENTER, ANDROID_EDGE))
    return image


def _shape_mask(size: int, round_icon: bool) -> Image.Image:
    scale = 4
    mask = Image.new("L", (size * scale, size * scale), 0)
    draw = ImageDraw.Draw(mask)
    if round_icon:
        draw.ellipse((0, 0, size * scale - 1, size * scale - 1), fill=255)
    else:
        draw.rounded_rectangle((0, 0, size * scale - 1, size * scale - 1), radius=size * scale * 0.18, fill=255)
    return mask.resize((size, size), Image.Resampling.LANCZOS)


def write_android(master: Image.Image) -> None:
    radius = _enclosing_radius(master) / master.width  # fraction of the master's side
    # The emblem's side within a 108 dp layer so its enclosing circle fits the safe circle.
    adaptive_fraction = (ANDROID_SAFE_DIAMETER * ANDROID_SAFE_MARGIN / 2.0) / radius
    for density, legacy, layer in ANDROID_DENSITIES:
        folder = ANDROID_RES / f"mipmap-{density}"
        side = round(layer * adaptive_fraction)
        foreground = Image.new("RGBA", (layer, layer), (0, 0, 0, 0))
        foreground.alpha_composite(emblem(master, side), ((layer - side) // 2, (layer - side) // 2))
        write_png(folder / "ic_launcher_foreground.png", foreground)
        write_png(folder / "ic_launcher_background.png", _radial_background(layer))
        for name, round_icon in (("ic_launcher.png", False), ("ic_launcher_round.png", True)):
            icon = _radial_background(legacy).convert("RGBA")
            # Legacy icons have no mask margin: the emblem fills most of the shape.
            inner = round(legacy * (0.86 if round_icon else 0.90))
            icon.alpha_composite(emblem(master, inner), ((legacy - inner) // 2, (legacy - inner) // 2))
            icon.putalpha(_shape_mask(legacy, round_icon))
            write_png(folder / name, icon)
    adaptive = ANDROID_RES / "mipmap-anydpi-v26"
    adaptive.mkdir(parents=True, exist_ok=True)
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           "<!-- Generated by tools/triaevum_release/branding/generate_branding.py. -->\n"
           '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
           '    <background android:drawable="@mipmap/ic_launcher_background" />\n'
           '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
           "</adaptive-icon>\n")
    for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
        (adaptive / name).write_text(xml, encoding="utf-8", newline="\n")


def android_preview(path: Path) -> None:
    """A review sheet: the adaptive icon under the common launcher masks."""
    layer = 432
    foreground = Image.open(ANDROID_RES / "mipmap-xxxhdpi/ic_launcher_foreground.png").convert("RGBA")
    background = Image.open(ANDROID_RES / "mipmap-xxxhdpi/ic_launcher_background.png").convert("RGBA")
    full = background.copy()
    full.alpha_composite(foreground)
    visible = round(layer * 72 / 108)
    crop = full.crop(((layer - visible) // 2,) * 2 + ((layer + visible) // 2,) * 2)
    sheet = Image.new("RGBA", (visible * 4 + 100, visible + 260), (236, 236, 236, 255))
    draw = ImageDraw.Draw(sheet)
    masks = []
    for kind in ("circle", "squircle", "rounded", "safe"):
        mask = Image.new("L", (visible * 4, visible * 4), 0)
        mask_draw = ImageDraw.Draw(mask)
        if kind in ("circle", "safe"):
            mask_draw.ellipse((0, 0, visible * 4 - 1, visible * 4 - 1), fill=255)
        elif kind == "squircle":
            mask_draw.rounded_rectangle((0, 0, visible * 4 - 1, visible * 4 - 1), radius=visible * 4 * 0.42, fill=255)
        else:
            mask_draw.rounded_rectangle((0, 0, visible * 4 - 1, visible * 4 - 1), radius=visible * 4 * 0.16, fill=255)
        masks.append((kind, mask.resize((visible, visible), Image.Resampling.LANCZOS)))
    for index, (kind, mask) in enumerate(masks):
        tile = crop.copy()
        tile.putalpha(mask)
        x = 20 + index * (visible + 20)
        sheet.alpha_composite(tile, (x, 20))
        if kind == "safe":
            safe = visible * 66 / 72
            offset = (visible - safe) / 2
            draw.ellipse((x + offset, 20 + offset, x + offset + safe, 20 + offset + safe), outline=(255, 64, 64, 255),
                         width=2)
    x = 20
    for density, legacy, _layer in ANDROID_DENSITIES:
        for name in ("ic_launcher.png", "ic_launcher_round.png"):
            icon = Image.open(ANDROID_RES / f"mipmap-{density}" / name).convert("RGBA")
            sheet.alpha_composite(icon, (x, visible + 50))
            x += legacy + 12
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(path)


# --- main ------------------------------------------------------------------

def generate() -> list[Path]:
    game = Image.open(GAME_MASTER).convert("RGBA")
    forge = Image.open(FORGE_MASTER).convert("RGBA")
    written = []
    for name, master in (("triaevum.ico", game), ("forge.ico", forge)):
        write_ico(BRANDING / "windows" / name, master)
        written.append(BRANDING / "windows" / name)
    path = BRANDING / "forge/forge_window_icon.png"
    write_png(path, emblem(forge, FORGE_WINDOW_ICON_SIZE, padding=0.02))
    written.append(path)
    for size in LINUX_SIZES:
        for name, master in ((APP_ID, game), (FORGE_ICON_NAME, forge)):
            path = BRANDING / f"linux/hicolor/{size}x{size}/apps/{name}.png"
            # freedesktop icons keep a little room around a round emblem.
            write_png(path, emblem(master, size, padding=0.03))
            written.append(path)
    write_window_icon_inc(WINDOW_ICON_INC, game)
    written.append(WINDOW_ICON_INC)
    write_android(game)
    written.append(ANDROID_RES)
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--import-sources", type=Path, metavar="DIR",
                        help="rebuild the masters from the owner's original images in DIR first")
    parser.add_argument("--android-preview", type=Path, metavar="PNG",
                        help="also write a review sheet of the Android launcher icon")
    args = parser.parse_args()
    if args.import_sources is not None:
        import_sources(args.import_sources)
    for path in generate():
        print(path.relative_to(ROOT).as_posix())
    if args.android_preview is not None:
        android_preview(args.android_preview)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
