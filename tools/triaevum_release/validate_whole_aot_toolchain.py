"""Build and execute the real title ABI using only synthetic, redistributable input."""

import argparse
import json
import subprocess
import time
from pathlib import Path

try:
    from . import release_platform
    from .native_toolchain import cxx_driver_arguments
    from .common import atomic_write_bytes, atomic_write_json, sha256_file
    from .bundle_paths import distribution_root
    from .windows_sysroot import load_sysroot, build_environment
    from .whole_aot_plugin_backend import WholeAotPluginToolchain, build_whole_aot_plugin
except ImportError:
    import release_platform
    from native_toolchain import cxx_driver_arguments
    from common import atomic_write_bytes, atomic_write_json, sha256_file
    from bundle_paths import distribution_root
    from windows_sysroot import load_sysroot, build_environment
    from whole_aot_plugin_backend import WholeAotPluginToolchain, build_whole_aot_plugin

ROOT = distribution_root()


def validate(compiler: Path, archiver: Path, support: Path, include: Path,
             sysroot: Path | None, output: Path) -> dict:
    platform = release_platform.host_platform()
    windows = release_platform.is_windows(platform)
    if windows and sysroot is None:
        raise ValueError("A verified Windows sysroot is required for the Windows ABI probe")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    # mov r0,#1; add r0,r0,#2; str r0,[r1]; ldr r2,[r1]; bx lr.
    code = b"".join(word.to_bytes(4, "little") for word in
                    (0xE3A00001, 0xE2800002, 0xE5810000, 0xE5912000, 0xE12FFF1E))
    code_path = output / "synthetic.bin"
    atomic_write_bytes(code_path, code)
    program = output / "program.json"
    selection = output / "selection.json"
    atomic_write_json(program, {
        "format": "oot3d_whole_aot_program_v1", "base": 0x1000,
        "code_sha256": sha256_file(code_path),
        "blocks": [{"id": 0, "pc": 0x1000, "end_pc": 0x1014,
                    "successors": [{"site": 0x1010, "kind": "return"}]}],
        "functions": [{"entry": 0x1000, "name": "SyntheticFunction", "blocks": [0],
                       "closed_static_cfg": True, "direct_calls": [], "tail_calls": [], "indirect_sites": []}],
    })
    atomic_write_json(selection, {"format": "oot3d_whole_aot_function_selection_v1",
                                  "functions": [{"entry": 0x1000}]})
    verified = load_sysroot(sysroot) if windows else None
    built = build_whole_aot_plugin(
        program_path=program, selection_path=selection, code_path=code_path,
        cache_root=output / "cache", shard_count=1, jobs=1,
        toolchain=WholeAotPluginToolchain(compiler, archiver, support, include,
            target_triple=platform.target, profile=platform.profile, sysroot=sysroot),
        sysroot_contract=verified)
    environment = build_environment() if windows else None
    includes = (ROOT / "tools/oot3d/native_game_runtime", ROOT / "tools/oot3d/native_a32_runtime",
                ROOT / "tools/oot3d/native_a32_runtime/upstream", include.resolve())
    probe_source = ROOT / "tools/triaevum_release/whole_aot_abi_probe.cpp"
    if windows:
        executable = output / "abi-probe.exe"
        command = [str(compiler.resolve()), "/nologo", "/EHsc", "/MT", "/std:c++20", "/O2",
                   "/fp:strict", "/DNOMINMAX", "-fuse-ld=lld", *verified.arguments(),
                   *[f"/I{path}" for path in includes], str(probe_source),
                   "/c", f"/Fo{output / 'abi-probe.obj'}"]
        link = [str(compiler.resolve()), "/nologo", "/MT", "-fuse-ld=lld", *verified.arguments(),
                str(output / "abi-probe.obj"), str(support.resolve()), f"/Fe{executable}"]
    else:
        executable = output / "abi-probe"
        command = [str(compiler.resolve()), *cxx_driver_arguments(platform.target),
                   "-std=c++20", "-O2", "-ffp-model=strict", "-DNOMINMAX",
                   *[f"-I{path}" for path in includes], "-c", str(probe_source),
                   "-o", str(output / "abi-probe.o")]
        link = [str(compiler.resolve()), *cxx_driver_arguments(platform.target),
                "-fuse-ld=lld", str(output / "abi-probe.o"),
                str(support.resolve()), "-ldl", "-o", str(executable)]
    for name, arguments in (("compile", command), ("link", link),
                            ("execute", [str(executable), built["plugin"]])):
        atomic_write_json(output / f"{name}-command.json", arguments)
        result = subprocess.run(arguments, cwd=output, env=environment,
                                capture_output=True, text=True, timeout=120, check=False)
        atomic_write_bytes(output / f"{name}.log", (result.stdout + result.stderr).encode())
        if result.returncode:
            raise ValueError(f"Native ABI {name} failed ({result.returncode}); see {output / (name + '.log')}")
    checks = json.loads(result.stdout)
    if not all(checks.get(key) is True for key in
               ("arithmetic", "memory", "callback", "tls", "observable_exit", "abi_rejection")):
        raise ValueError("Native ABI probe did not establish all checks")
    receipt = {"format": "triaevum_native_abi_probe_v1", "status": "passed", "checks": checks,
               "target": platform.target, "plugin": built["plugin"],
               "sysroot_identity": verified.identity if verified else None, "plugin_sha256": built["plugin_sha256"],
               "support_sha256": sha256_file(support), "compiler_sha256": sha256_file(compiler),
               "probe_sha256": sha256_file(executable), "build_status": built["status"],
               "elapsed_seconds": time.perf_counter() - started, "scope": "synthetic_real_abi_not_full_title"}
    atomic_write_json(output / "receipt.json", receipt)
    return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "archiver", "support", "include", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--sysroot", type=Path, help="Verified Windows sysroot (Windows only)")
    args = parser.parse_args()
    print(json.dumps(validate(**vars(args)), indent=2))
