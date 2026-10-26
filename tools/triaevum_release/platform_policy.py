"""Compose common publication rules with one explicit platform allowlist.

This is metadata validation, not host detection or binary execution. Future
platforms must supply an adapter and policy; they cannot inherit Windows rules.

A package also names its Forge profile (the release manifest's
`release.forge_profile`; manifests that predate the field are the PyInstaller
profile). A profile only adds the roles, required files and checks of one
Forge build; it cannot remove a common or platform rule.
"""

from copy import deepcopy

try:
    from .release_platform import for_target
except ImportError:
    from release_platform import for_target


_LIST_KEYS = ("required_paths", "forbidden_basenames", "forbidden_extensions",
              "source_archive_required_paths", "rust_forge_binary_roles",
              "forbidden_package_patterns", "binary_host_path_markers", "forbidden_roles")


def forge_profile_name(policy: dict, release: dict | None = None) -> str:
    profile = (release or {}).get("forge_profile", policy.get("default_forge_profile"))
    if not isinstance(profile, str) or profile not in policy.get("forge_profiles", {}):
        raise ValueError(f"Release policy has no Forge profile {profile!r}")
    return profile


def _merge(result: dict, layer: dict, origin: str) -> None:
    allowed = result["allowed_roles"]
    for role, paths in layer.get("allowed_roles", {}).items():
        if role in allowed:
            raise ValueError(f"{origin} duplicates common role: {role}")
        allowed[role] = list(paths)
    for key in _LIST_KEYS:
        if key in layer:
            result[key] = [*result.get(key, []), *layer[key]]


def source_policy(policy: dict, forge_profile: str) -> dict:
    """The corresponding-source requirements of a package with this Forge profile."""
    result = deepcopy(policy)
    profile = policy.get("forge_profiles", {}).get(forge_profile)
    if not isinstance(profile, dict):
        raise ValueError(f"Release policy has no Forge profile {forge_profile!r}")
    result["source_archive_required_paths"] = [*result.get("source_archive_required_paths", []),
                                               *profile.get("source_archive_required_paths", [])]
    return result


def resolve_policy(policy: dict, target: str, forge_profile: str | None = None) -> dict:
    for_target(target)  # Reject unimplemented adapters, including Android today.
    platform = policy.get("platforms", {}).get(target)
    if not isinstance(platform, dict):
        raise ValueError(f"Release policy has no platform contract for {target}")
    profile = forge_profile or forge_profile_name(policy)
    result = deepcopy(policy)
    _merge(result, {key: platform[key] for key in ("allowed_roles", "required_paths", "forbidden_basenames",
                                                   "forbidden_extensions") if key in platform},
           "Platform policy")
    for origin, layers in (("Forge profile", policy.get("forge_profiles", {})),
                           ("Platform Forge profile", platform.get("forge_profiles", {}))):
        layer = layers.get(profile)
        if not isinstance(layer, dict):
            raise ValueError(f"Release policy has no Forge profile {profile!r} for {target}")
        _merge(result, layer, origin)
    result["forge_profile"] = profile
    return result
