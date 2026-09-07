"""Select a private prepared generation without silently accepting stale proof."""

from pathlib import Path
from typing import Callable

try:
    from .common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from .cache_lock import acquire_cache_lock, release_cache_lock
    from .windows_sysroot import load_sysroot
except ImportError:
    from common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from cache_lock import acquire_cache_lock, release_cache_lock
    from windows_sysroot import load_sysroot


def select_sysroot(generation: Path, *, compiler: Path, support: Path,
                   reprobe: Callable[[Path], dict] | None = None) -> Path | None:
    if not generation.exists() and not generation.is_symlink():
        return None
    if generation.is_symlink():
        raise ValueError("Prepared toolchain generation must not be a link")
    generation = generation.resolve(strict=True)
    receipt = load_json_object(generation / "toolchain.json")
    if (receipt.get("format") != "triaevum_prepared_toolchain_v1"
            or receipt.get("status") != "native_probe_passed"):
        raise ValueError("Prepared toolchain has no successful native proof")
    proof = receipt.get("native_probe", {})
    checks = proof.get("checks", {})
    if proof.get("status") != "passed" or not all(checks.get(name) is True for name in (
            "arithmetic", "memory", "callback", "tls", "observable_exit", "abi_rejection")):
        raise ValueError("Prepared toolchain native proof is incomplete")
    current = {field: sha256_file(path) for path, field in (
        (compiler, "compiler_sha256"), (support, "support_sha256"))}
    stale = [field for field, value in current.items() if proof.get(field) != value]
    root = generation / normalize_relative_path(receipt["sysroot"])
    if root.is_symlink() or not root.resolve().is_relative_to(generation):
        raise ValueError("Prepared sysroot escaped its generation")
    verified = load_sysroot(root)
    if verified.identity != receipt.get("identity") or verified.identity != proof.get("sysroot_identity"):
        raise ValueError("Prepared sysroot identity changed")
    if stale:
        if reprobe is None:
            raise ValueError("Prepared toolchain proof is stale for " + ", ".join(stale))
        lock = acquire_cache_lock(generation / ".proof.lock")
        try:
            updated = reprobe(verified.root)
            if (updated.get("status") != "passed"
                    or updated.get("sysroot_identity") != verified.identity
                    or any(updated.get(field) != value for field, value in current.items())
                    or not all(updated.get("checks", {}).get(name) is True for name in checks)):
                raise ValueError("Updated toolchain native proof is invalid")
            # Publish only a complete real proof; failed probes preserve the
            # previous generation and its acquisition/license provenance.
            receipt["native_probe"] = updated
            atomic_write_json(generation / "toolchain.json", receipt)
        finally:
            release_cache_lock(lock)
    return verified.root
