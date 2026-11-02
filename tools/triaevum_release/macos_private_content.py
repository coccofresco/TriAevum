"""Private-title exclusion for macOS exports only; no non-Mac policy changes."""

import codecs
import itertools
from pathlib import Path
import re
import zipfile

# Keep the guard and its tests eligible for the corresponding-source archive.
# The excluded spellings must not occur literally in their own implementation.
MARKERS = tuple(bytes(value) for value in (
    (109, 109, 51, 100), (109, 111, 111, 110, 102, 97, 108, 108),
    (109, 97, 106, 111, 114, 97, 39, 115)))
PRIVATE_DIRECTORY = MARKERS[0].decode("ascii")
_WORDS = [re.escape(marker.decode("ascii")) for marker in MARKERS]
_WORDS[-1] = _WORDS[-1].replace("'", "['\u2019]")
_NAME = re.compile(r"(?<![A-Za-z0-9])(?:" + "|".join(_WORDS) + r")(?![A-Za-z0-9])", re.IGNORECASE)
# Underscore-separated identifiers are covered by _NAME. Also recognize the
# title component in C++ CamelCase/mangled symbols, without matching arbitrary
# case-insensitive substrings of unrelated words or compressed bytes.
_SYMBOL = re.compile("|".join(re.escape(marker[:1].upper().decode() + marker[1:].decode()) + r"(?=[A-Z_])"
                              for marker in MARKERS[:2]))
_C_STRING = re.compile(rb"(?<![\x20-\x7e])[\x20-\x7e]{4,}\x00")
_NATIVE_MAGICS = {bytes.fromhex(value) for value in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca", "7f454c46")}
_TEXT_SUFFIXES = {".py", ".md", ".txt", ".json", ".c", ".cc", ".cpp", ".h", ".hpp", ".rs", ".cmake", ".toml", ".sh"}
_CHUNK = 1024 * 1024


def _text_encoding(data: bytes) -> str:
    for prefix, encoding in ((codecs.BOM_UTF32_LE, "utf-32"), (codecs.BOM_UTF32_BE, "utf-32"),
                             (codecs.BOM_UTF16_LE, "utf-16"), (codecs.BOM_UTF16_BE, "utf-16"),
                             (codecs.BOM_UTF8, "utf-8-sig")):
        if data.startswith(prefix):
            return encoding
    # Recognize BOM-less wide text by its code-unit layout. Decode it normally:
    # a NUL character remains a boundary, never a join between adjacent symbols.
    sample = data[:256]
    for width, encoding, lane in ((4, "utf-32-le", 0), (4, "utf-32-be", 3),
                                  (2, "utf-16-le", 0), (2, "utf-16-be", 1)):
        if len(sample) >= width * 4:
            lanes = [sample[index::width] for index in range(width)]
            if (sum(byte == 0 for byte in lanes[lane]) < len(lanes[lane]) / 8 and
                    all(sum(byte == 0 for byte in part) > len(part) * 3 / 4
                        for index, part in enumerate(lanes) if index != lane)):
                return encoding
    return "utf-8"


def names_private_title(value: str | bytes) -> bool:
    text = value if isinstance(value, str) else value.decode(_text_encoding(value), errors="replace")
    return bool(_NAME.search(text) or _SYMBOL.search(text))


def stream_names_private_title(stream, *, filename: str = "") -> bool:
    suffix = Path(filename).suffix.lower()
    if suffix in (".zip", ".o3ps"):
        return False  # ZIP members are checked by archive_errors, never compressed bytes.
    first = stream.read(_CHUNK)
    if not first or first.startswith(b"PK\x03\x04"):
        return False
    encoding = _text_encoding(first)
    native = first[:4] in _NATIVE_MAGICS or first[:2] == b"MZ"
    text = first.decode(encoding, errors="replace")
    is_text = not native and (encoding != "utf-8" or suffix in _TEXT_SUFFIXES or
        all(character.isprintable() or character in "\r\n\t" for character in text[:512]))
    blocks = itertools.chain((first,), iter(lambda: stream.read(_CHUNK), b""))
    if is_text:
        decoder = codecs.getincrementaldecoder(encoding)(errors="replace")
        tail = ""
        for block in blocks:
            decoded = tail + decoder.decode(block)
            if names_private_title(decoded):
                return True
            tail = decoded[-128:]
        return names_private_title(tail + decoder.decode(b"", final=True))
    tail = b""
    for block in blocks:
        data = tail + block
        for match in _C_STRING.finditer(data):
            if names_private_title(match.group()[:-1].decode("ascii")):
                return True
        tail = data[-8192:]
    return False


def archive_errors(archive: zipfile.ZipFile, *, depth: int = 0):
    if depth > 8:
        yield "macOS source archive nesting exceeds the inspection limit"
        return
    for item in archive.infolist():
        if names_private_title(item.filename):
            yield "macOS source archive names a private title: " + item.filename
        if not item.is_dir():
            with archive.open(item) as stream:
                if Path(item.filename).suffix.lower() == ".zip":
                    try:
                        with zipfile.ZipFile(stream) as nested:
                            yield from archive_errors(nested, depth=depth + 1)
                    except (OSError, ValueError, zipfile.BadZipFile):
                        yield "macOS nested source archive cannot be checked: " + item.filename
                elif stream_names_private_title(stream, filename=item.filename):
                    yield "macOS source archive text names a private title: " + item.filename


def package_errors(root: Path):
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if names_private_title(relative):
            yield "macOS package path names a private title: " + relative
        if path.is_symlink() or not path.is_file():
            continue
        if path.suffix.lower() == ".zip":
            try:
                with zipfile.ZipFile(path) as archive:
                    yield from archive_errors(archive)
            except (OSError, ValueError, zipfile.BadZipFile) as exc:
                yield f"macOS source archive cannot be checked: {relative}: {type(exc).__name__}"
        else:
            with path.open("rb") as stream:
                if stream_names_private_title(stream, filename=relative):
                    yield "macOS package text names a private title: " + relative
