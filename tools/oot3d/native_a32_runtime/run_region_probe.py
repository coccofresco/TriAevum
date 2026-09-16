"""Build/run the opt-in region lowering against its original native body."""
import argparse
from pathlib import Path
import subprocess
from whole_aot_cpp import Block, Function, Instruction, _render_region_function


def main():
    parser = argparse.ArgumentParser(__doc__)
    for name in ("output", "compiler", "support", "include"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    args.output.mkdir(parents=True, exist_ok=True)
    # Four iterations, observable loads/stores, conditional loop and return.
    words = (0xE5912000, 0xE2822001, 0xE5812000, 0xE2500001,
             0xE1A03002, 0x1AFFFFF9)
    function = Function(0x1000, "Loop", (
        Block(0, 0x1000, 0x1018,
              tuple(Instruction(0x1000 + i * 4, raw) for i, raw in enumerate(words)),
              ({"kind": "branch", "target": 0x1000, "condition": "ne"},
               {"kind": "fallthrough", "target": 0x1018})),
        Block(1, 0x1018, 0x101C, (Instruction(0x1018, 0xE12FFF1E),),
              ({"kind": "return"},)),
    ), (), (0x1018,))
    (args.output / "region_fixture.inc").write_text("\n".join(
        _render_region_function(function, {}, frozenset(), frozenset({0x1000}))))
    obj = args.output / "region_probe.obj"
    exe = args.output / "region_probe.exe"
    common = [str(args.compiler), "/nologo", "/MT"]
    subprocess.run([*common, "/c", "/O2", "/std:c++20", "/EHsc", "/DNOMINMAX",
                    *(f"/I{p}" for p in (args.output, here, here / "upstream",
                       here.parent / "native_game_runtime", args.include)),
                    str(here / "region_probe.cpp"), f"/Fo{obj}"], check=True, timeout=120)
    subprocess.run([*common, str(obj), str(args.support), f"/Fe{exe}",
                    "-fuse-ld=lld", "/link", "/NOIMPLIB"], check=True, timeout=120)
    subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
