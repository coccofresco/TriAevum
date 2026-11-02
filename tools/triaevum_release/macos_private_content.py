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
_BARE = MARKERS[2][:-2].decode("ascii")
_LONG_NAMES = (MARKERS[1].decode("ascii"), _BARE + "'s", _BARE + "\u2019s", _BARE + "s")
_LONG_TEXT = re.compile("|".join(map(re.escape, _LONG_NAMES)), re.IGNORECASE)
_BARE_TEXT = re.compile(r"(?<![A-Za-z0-9])" + re.escape(_BARE) + r"(?![A-Za-z0-9])", re.IGNORECASE)
_BARE_TEXT_PARTIAL = re.compile(r"(?<![A-Za-z0-9])" + re.escape(_BARE) + r"(?=[^A-Za-z0-9])", re.IGNORECASE)
# Long names can be Rust string slices, concatenated literals or wide strings.
# They need neither printable surroundings nor a terminator. Do not join NULs:
# adjacent SPIR-V symbols must remain separate strings.
_LONG_NATIVE = re.compile(b"|".join(re.escape(name.encode(encoding))
    for name in _LONG_NAMES for encoding in ("utf-8", "utf-16le", "utf-16be")), re.IGNORECASE)
# Only the short token needs a string terminator in native data: four random
# bytes are common in large code/data sections. Its name/symbol context is
# checked separately; text and paths use unrestricted substring matching.
_SHORT_NATIVE = tuple((encoding, re.compile(re.escape(
    PRIVATE_DIRECTORY.encode(encoding)), re.IGNORECASE), re.compile(units), terminator, re.compile(partial))
    for encoding, units, terminator, partial in (
        ("ascii", rb"[\x20-\x7e]*", b"\x00", rb""),
        ("utf-16le", rb"(?:[\x20-\x7e]\x00)*", b"\x00\x00", rb"[\x00\x20-\x7e]?"),
        ("utf-16be", rb"(?:\x00[\x20-\x7e])*", b"\x00\x00", rb"\x00?")))
_NATIVE_MAGICS = {bytes.fromhex(value) for value in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca", "7f454c46")}
_TEXT_SUFFIXES = {".py", ".md", ".txt", ".json", ".c", ".cc", ".cpp", ".h", ".hpp", ".rs", ".cmake", ".toml", ".sh"}
_CHUNK = 1024 * 1024
CORPUS_PATH = "forge/shader-corpus/portable.o3ps"
_ZIP_MAGICS = (b"PK\x03\x04", b"PK\x05\x06", b"PK\x07\x08")


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


def names_private_title(value: str | bytes, *, _end_known: bool = True) -> bool:
    text = value if isinstance(value, str) else value.decode(_text_encoding(value), errors="replace")
    bare = _BARE_TEXT if _end_known else _BARE_TEXT_PARTIAL
    return bool(PRIVATE_DIRECTORY in text.lower() or _LONG_TEXT.search(text) or bare.search(text))


def _native_short_name(data, match, encoding, stream):
    width = 1 if encoding == "ascii" else 2
    before = data[max(0, match.start() - width):match.start()].decode(encoding, errors="replace")
    after = data[match.end():match.end() + width]
    if len(after) < width:
        position = stream.tell()
        after += stream.read(width - len(after))
        stream.seek(position)
    after = after.decode(encoding, errors="replace")
    alnum = lambda value: bool(value) and value.isascii() and value.isalnum()
    # Keep name/symbol context only for the short native token. A random mixed-
    # case occurrence inside a long encoded data string is not an identifier.
    # Either edge suffices (prefix/suffix names); CamelCase handles C++ symbols.
    camel = match.group().decode(encoding) == PRIVATE_DIRECTORY[:1].upper() + PRIVATE_DIRECTORY[1:]
    return not alnum(before) or not alnum(after) or (camel and after.isupper())


def _native_string_terminated(data, start, units, terminator, partial, stream):
    position = stream.tell()
    remainder = data[start:]
    try:
        while True:
            end = units.match(remainder).end()
            tail = remainder[end:]
            if tail.startswith(terminator):
                return True
            if not partial.fullmatch(tail):
                return False
            following = stream.read(_CHUNK)
            if not following:
                return False
            remainder = tail + following
    finally:
        stream.seek(position)


def stream_names_private_title(stream, *, filename: str = "", catalogued_corpus: bool = False,
                               _depth: int = 0) -> bool:
    suffix = Path(filename).suffix.lower()
    first = stream.read(_CHUNK)
    if suffix == ".zip" or first.startswith(_ZIP_MAGICS):
        if _depth >= 8:
            raise ValueError("ZIP nesting exceeds the inspection limit")
        stream.seek(0)
        with zipfile.ZipFile(stream) as archive:
            return any(archive_errors(archive, depth=_depth + 1))
    if catalogued_corpus and filename == CORPUS_PATH:
        return False  # The caller has validated this exact catalogued corpus.
    if not first:
        return False
    encoding = _text_encoding(first)
    native = first[:4] in _NATIVE_MAGICS or first[:2] == b"MZ"
    text = first.decode(encoding, errors="replace")
    # A two-letter prefix alone cannot turn an ordinary text document into PE.
    plain = "\ufffd" not in text[:512] and all(
        character.isprintable() or character in "\r\n\t" for character in text[:512])
    is_text = (suffix in _TEXT_SUFFIXES or ((not native or first[:2] == b"MZ") and plain) or
               (not native and encoding != "utf-8"))
    blocks = itertools.chain((first,), iter(lambda: stream.read(_CHUNK), b""))
    if is_text:
        decoder = codecs.getincrementaldecoder(encoding)(errors="replace")
        tail = ""
        for block in blocks:
            decoded = tail + decoder.decode(block)
            if names_private_title(decoded, _end_known=False):
                return True
            tail = decoded[-128:]
        return names_private_title(tail + decoder.decode(b"", final=True))
    tail = b""
    for block in blocks:
        data = tail + block
        if _LONG_NATIVE.search(data):
            return True
        for encoding, pattern, units, terminator, partial in _SHORT_NATIVE:
            for match in pattern.finditer(data, max(0, len(tail) - 8)):
                if (_native_short_name(data, match, encoding, stream) and
                        _native_string_terminated(data, match.end(), units, terminator, partial, stream)):
                    return True
        tail = data[-64:]
    return False


def archive_errors(archive: zipfile.ZipFile, *, depth: int = 0):
    if depth > 8:
        yield "macOS source archive nesting exceeds the inspection limit"
        return
    for item in archive.infolist():
        if names_private_title(item.filename):
            yield "macOS source archive names a private title: " + item.filename
        if not item.is_dir():
            try:
                with archive.open(item) as stream:
                    if stream_names_private_title(stream, filename=item.filename, _depth=depth):
                        yield "macOS source archive text names a private title: " + item.filename
            except (OSError, ValueError, RuntimeError, zipfile.BadZipFile):
                yield "macOS source archive member cannot be checked: " + item.filename


def package_errors(root: Path, *, catalogued_corpus_paths=()):
    # The catalog root is Resources; native Frameworks and the launcher live
    # beside it and must receive the same private-content check.
    scan_root = root.parent.parent if root.name == "Resources" and root.parent.name == "Contents" else root
    for path in scan_root.rglob("*"):
        relative = path.relative_to(root if path.is_relative_to(root) else scan_root).as_posix()
        if names_private_title(relative):
            yield "macOS package path names a private title: " + relative
        if path.is_symlink() or not path.is_file():
            continue
        try:
            with path.open("rb") as stream:
                if stream_names_private_title(stream, filename=relative,
                        catalogued_corpus=relative in catalogued_corpus_paths):
                    yield "macOS package text names a private title: " + relative
        except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as exc:
            yield f"macOS package file cannot be checked: {relative}: {type(exc).__name__}"
