from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .binary import BinaryView, ParseError


@dataclass(frozen=True)
class ZarFile:
    index: int
    name: str
    type_name: str
    type_local_index: int | None
    offset: int
    size: int


@dataclass(frozen=True)
class ZarArchive:
    path: str
    files: tuple[ZarFile, ...]
    _data: bytes

    @classmethod
    def from_path(cls, path: Path) -> "ZarArchive":
        return cls.parse(path.read_bytes(), str(path))

    @classmethod
    def parse(cls, data: bytes, source: str = "<memory>") -> "ZarArchive":
        view = BinaryView(data, source)
        if view.bytes(0, 4) != b"ZAR\x01":
            raise ParseError(f"{source}: expected ZAR magic")

        archive_size = view.u32(0x04)
        type_count = view.u16(0x08)
        file_count = view.u16(0x0A)
        type_section = view.u32(0x0C)
        meta_section = view.u32(0x10)
        data_section = view.u32(0x14)
        if archive_size > len(data):
            raise ParseError(
                f"{source}: header size 0x{archive_size:x} exceeds file length 0x{len(data):x}"
            )

        names: list[str] = []
        sizes: list[int] = []
        for index in range(file_count):
            entry = meta_section + index * 8
            sizes.append(view.u32(entry))
            name_off = view.u32(entry + 4)
            names.append(read_c_string(view, name_off))

        type_for_file = ["unknown"] * file_count
        type_local_index_for_file: list[int | None] = [None] * file_count
        for type_index in range(type_count):
            entry = type_section + type_index * 0x10
            typed_count = view.u32(entry)
            list_off = view.u32(entry + 4)
            name_off = view.u32(entry + 8)
            type_name = read_c_string(view, name_off)
            for typed_file_index in range(typed_count):
                file_index = view.u32(list_off + typed_file_index * 4)
                if file_index >= file_count:
                    raise ParseError(
                        f"{source}: ZAR type {type_name} references invalid file {file_index}"
                    )
                type_for_file[file_index] = type_name
                type_local_index_for_file[file_index] = typed_file_index

        data_offsets = [view.u32(data_section + index * 4) for index in range(file_count)]

        files: list[ZarFile] = []
        for index, (name, size, data_offset) in enumerate(zip(names, sizes, data_offsets)):
            view.require(data_offset, size)
            files.append(
                ZarFile(
                    index=index,
                    name=name,
                    type_name=type_for_file[index],
                    type_local_index=type_local_index_for_file[index],
                    offset=data_offset,
                    size=size,
                )
            )

        return cls(path=source, files=tuple(files), _data=data)

    def read_file(self, file: ZarFile) -> bytes:
        return self._data[file.offset : file.offset + file.size]

    def cmb_files(self) -> list[ZarFile]:
        return [file for file in self.files if file.type_name == "cmb" or file.name.endswith(".cmb")]

    def summary(self) -> dict[str, object]:
        counts: dict[str, int] = {}
        for file in self.files:
            counts[file.type_name] = counts.get(file.type_name, 0) + 1
        return {
            "path": self.path,
            "format": "zar",
            "file_count": len(self.files),
            "type_counts": counts,
            "files": [
                {
                    "index": file.index,
                    "name": file.name,
                    "type": file.type_name,
                    "type_local_index": file.type_local_index,
                    "size": file.size,
                    "offset": file.offset,
                }
                for file in self.files
            ],
        }


def read_c_string(view: BinaryView, offset: int) -> str:
    end = offset
    while end < len(view.data) and view.data[end] != 0:
        end += 1
    view.require(offset, end - offset)
    return view.data[offset:end].decode("ascii", errors="replace")
