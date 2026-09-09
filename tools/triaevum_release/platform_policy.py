"""Compose common publication rules with one explicit platform allowlist.

This is metadata validation, not host detection or binary execution. Future
platforms must supply an adapter and policy; they cannot inherit Windows rules.
"""

from copy import deepcopy

try:
    from .release_platform import for_target
except ImportError:
    from release_platform import for_target


def resolve_policy(policy: dict, target: str) -> dict:
    for_target(target)  # Reject unimplemented adapters, including Android today.
    platform = policy.get("platforms", {}).get(target)
    if not isinstance(platform, dict):
        raise ValueError(f"Release policy has no platform contract for {target}")
    result = deepcopy(policy)
    allowed = result["allowed_roles"]
    for role, paths in platform.get("allowed_roles", {}).items():
        if role in allowed:
            raise ValueError(f"Platform policy duplicates common role: {role}")
        allowed[role] = list(paths)
    for key in ("required_paths", "forbidden_basenames", "forbidden_extensions"):
        result[key] = [*result.get(key, []), *platform.get(key, [])]
    return result
