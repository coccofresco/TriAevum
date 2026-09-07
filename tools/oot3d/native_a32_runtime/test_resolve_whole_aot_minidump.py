import struct

from resolve_whole_aot_minidump import parse_minidump, resolve_minidump


def _minidump(exception_rva, timestamp=0x12345678):
    module_base = 0x7FF600000000
    module_name = r"C:\game\oot3d_native_game.exe".encode("utf-16-le")
    data = bytearray(0x400)
    struct.pack_into("<4sIIIIIQ", data, 0, b"MDMP", 0xA793, 2, 0x20,
                     0, 0x01020304, 0)
    struct.pack_into("<III", data, 0x20, 6, 160, 0x80)
    struct.pack_into("<III", data, 0x2C, 4, 112, 0x140)
    struct.pack_into("<I", data, 0x80, 42)
    struct.pack_into("<I", data, 0x88, 0xC0000005)
    struct.pack_into("<Q", data, 0x98, module_base + exception_rva)
    struct.pack_into("<I", data, 0x140, 1)
    struct.pack_into("<QIIII", data, 0x144, module_base, 0x4000, 0,
                     timestamp, 0x240)
    struct.pack_into("<I", data, 0x240, len(module_name))
    data[0x244:0x244 + len(module_name)] = module_name
    return bytes(data)


def _guest_map():
    return {
        "native_image_timestamp": 0x12345678,
        "native_image_size": 0x4000,
        "functions": [
            {
                "entry": 0x100000,
                "name": "Gameplay_Update",
                "ranges": [[0x100000, 0x100020]],
                "native_symbol": "Execute_Gameplay_Update_00100000",
                "native_rva": 0x1000,
                "native_end_rva": 0x1100,
                "native_size": 0x100,
                "shard": 7,
                "shard_file": "shard_007.cpp",
            }
        ],
    }


def test_parse_and_resolve_whole_aot_exception():
    dump = parse_minidump(_minidump(0x1050))
    result = resolve_minidump(dump, _guest_map())

    assert result["exception"]["code_hex"] == "0xC0000005"
    assert result["module"]["rva"] == 0x1050
    assert result["image_matches_symbols"] is True
    assert result["whole_aot"]["resolved"] is True
    assert result["whole_aot"]["guest_owner_entry"] == 0x100000
    assert result["whole_aot"]["native_offset"] == 0x50


def test_resolver_reports_host_symbol_without_claiming_guest_owner():
    dump = parse_minidump(_minidump(0x2050))
    link_map = """
 Preferred load address is 0000000140000000
 0001:00002000 ?WorkerMain@Impl@@AEAAXXZ 0000000140002000 renderer.obj
"""
    result = resolve_minidump(dump, _guest_map(), link_map)

    assert result["whole_aot"]["resolved"] is False
    assert result["nearest_native_symbol"]["symbol"] == "?WorkerMain@Impl@@AEAAXXZ"
    assert result["nearest_native_symbol"]["offset"] == 0x50


def test_resolver_rejects_symbols_for_different_image():
    dump = parse_minidump(_minidump(0x1050, timestamp=0x87654321))
    result = resolve_minidump(dump, _guest_map())

    assert result["image_matches_symbols"] is False
    assert result["whole_aot"]["reason"] == "module_timestamp_or_size_mismatch"
