"""Container-independent import of a decoded title input directory.

This is an input transport, not a compatibility decision. Recipe verification
and the precompiled module catalog remain authoritative after staging.
"""
from pathlib import Path
import shutil

try:
    from . import ctr_rom
except ImportError:
    import ctr_rom


def stage_directory(source: Path, output: Path) -> ctr_rom.ExtractedTitleInputs:
    source = source.expanduser()
    if source.is_symlink() or not source.is_dir():
        raise ctr_rom.CtrRomError("Select a regular extracted-data directory")
    source = source.resolve()
    paths = {kind: source / f"{kind}.bin" for kind in ("code", "exheader", "romfs")}
    for kind, path in paths.items():
        if path.is_symlink() or not path.is_file() or path.resolve().parent != source:
            raise ctr_rom.CtrRomError(f"Extracted data requires a local {kind}.bin file")
    if paths["exheader"].stat().st_size != ctr_rom.EXHEADER_SIZE:
        raise ctr_rom.CtrRomError("Extracted ExHeader must contain exactly 2048 bytes")
    if not 0 < paths["code"].stat().st_size <= ctr_rom.MAX_DECOMPRESSED_CODE_SIZE:
        raise ctr_rom.CtrRomError("Extracted code.bin has an invalid size")
    with paths["romfs"].open("rb") as stream:
        if stream.read(4) != b"IVFC":
            raise ctr_rom.CtrRomError("Extracted romfs.bin must be a decrypted IVFC image")
    output = output.expanduser().resolve()
    if output == source or source in output.parents:
        raise ctr_rom.CtrRomError("Staging must be outside the source directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()  # Never reuse or clean up a directory not created here.
    try:
        staged = {}
        for kind, path in paths.items():
            with path.open("rb") as stream:
                staged[kind] = ctr_rom._copy_region(
                    stream, 0, path.stat().st_size, output / path.name)
        header = staged["exheader"].path.read_bytes()
        return ctr_rom.ExtractedTitleInputs(
            container_kind="EXTRACTED", partition_index=0,
            program_id=ctr_rom._u64(header, 0x200), **staged)
    except BaseException:
        shutil.rmtree(output, ignore_errors=True)
        raise
