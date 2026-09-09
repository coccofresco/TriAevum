"""A bounded compile/link/execute test of Forge's actual C++ prerequisites."""

from __future__ import annotations

import os
import subprocess
import tempfile
import time
from pathlib import Path

try:
    from . import release_platform
    from .native_toolchain import cxx_driver_arguments
    from .bundle_paths import distribution_path
    from .windows_sysroot import compiler_sysroot, build_environment
except ImportError:
    import release_platform
    from native_toolchain import cxx_driver_arguments
    from bundle_paths import distribution_path
    from windows_sysroot import compiler_sysroot, build_environment


def probe_toolchain(compiler: Path, archiver: Path, support: Path, include: Path,
                    *, sysroot: Path | None = None) -> dict:
    host = release_platform.host_platform()
    windows = release_platform.is_windows(host)
    paths = {
        "compiler": compiler, "archiver": archiver,
        "linker": compiler.with_name("lld-link.exe" if windows else "ld.lld"), "support": support,
        "json_header": include / "nlohmann/json.hpp",
    }
    missing = [name for name, path in paths.items() if not path.is_file()]
    if missing:
        raise ValueError("Forge toolchain is incomplete: " + ", ".join(missing))
    started = time.perf_counter()
    verified_sysroot = compiler_sysroot(compiler, sysroot) if windows else None
    source = distribution_path("tools/triaevum_release/toolchain_probe.cpp")
    with tempfile.TemporaryDirectory(prefix="triaevum-probe-") as directory:
        root = Path(directory)
        if windows:
            exe = root / "probe.exe"
            compile_command = [
                str(compiler.resolve()), f"--target={host.target}", "/nologo",
                "/std:c++20", "/EHsc", "/MT", "/O2", "/fp:strict", "-fuse-ld=lld",
                *(verified_sysroot.arguments() if verified_sysroot else ()),
                f"/I{include.resolve()}", f"/Fo{root / 'probe.obj'}", f"/Fe{exe}",
                str(source.resolve())]
        else:
            exe = root / "probe"
            compile_command = [
                str(compiler.resolve()), *cxx_driver_arguments(host.target), "-std=c++20", "-O2",
                "-ffp-model=strict", "-fuse-ld=lld", f"-I{include.resolve()}",
                "-o", str(exe), str(source.resolve())]
        commands = (compile_command, [str(exe)])
        for command in commands:
            try:
                result = subprocess.run(command, cwd=root, capture_output=True,
                                        text=True, timeout=30, check=False,
                                        env=build_environment() if verified_sysroot else None)
            except (OSError, subprocess.TimeoutExpired) as exc:
                raise ValueError(f"Forge toolchain probe failed: {exc}") from exc
            if result.returncode:
                raise ValueError(
                    f"Forge C++ compile/link/execute probe failed ({result.returncode}): "
                    f"{(result.stderr or result.stdout)[-4000:]}"
                )
    return {"status": "passed", "elapsed_seconds": time.perf_counter() - started,
            "scope": "current_machine", "hermetic_environment_verified": False,
            "sysroot_identity": verified_sysroot.identity if verified_sysroot else None,
            "dependency_resolution": "explicit_sysroot" if verified_sysroot else "host_discovery"}


def recommended_compile_jobs() -> int:
    # Default headroom for the desktop and the ThinLTO linker. It is not a
    # promise about per-shard memory: build telemetry must refine this budget.
    maximum = min(4, max(1, (os.cpu_count() or 2) - 2))
    if os.name == "nt":
        import ctypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                        *[(name, ctypes.c_ulonglong) for name in
                          ("total", "available", "page_total", "page_available",
                           "virtual_total", "virtual_available", "extended")]]

        memory = MemoryStatus()
        memory.length = ctypes.sizeof(memory)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
            available = max(0, memory.available - 2 * 1024**3)
            maximum = min(maximum, max(1, available // (1536 * 1024**2)))
    return maximum
