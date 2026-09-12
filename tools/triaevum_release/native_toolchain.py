"""Developer-only compiler policy; artifact identities live in release_platform."""

try:
    from .release_platform import for_target, is_windows
except ImportError:
    from release_platform import for_target, is_windows


def cxx_driver_arguments(target: str) -> tuple[str, ...]:
    platform = for_target(target)
    # Hashing resolves clang++ symlinks to clang. Preserve C++ link semantics
    # independently of argv[0], including when the input consists only of .o files.
    mode = () if is_windows(platform) else ("--driver-mode=g++",)
    return (*mode, f"--target={target}")
