"""A signed Linux package fixture for the bundle stager tests (a throwaway Ed25519 key in the test folder)."""
import functools
import json
from pathlib import Path

import audit_release as audit_module
import release_signature
from test_platform_policy import write_linux_package


def write_signed_linux_package(root: Path, scratch: Path) -> Path:
    """Writes the Linux package fixture under `root`, signs its manifest with a
    new key under `scratch`; returns the key list that trusts that key."""
    write_linux_package(root)
    private_key = scratch / "test-key.pem"
    entry = release_signature.generate_key(private_key, "stager test")
    keys = scratch / "keys.json"
    keys.write_text(json.dumps({"format": release_signature.KEYS_FORMAT, "keys": [entry]}), encoding="utf-8")
    manifest = root / "release-manifest.json"
    signature = release_signature.sign_manifest(manifest.read_bytes(), private_key)
    (root / release_signature.NAME).write_text(json.dumps(signature), encoding="utf-8")
    return keys


def audit_trusting(keys: Path):
    """audit_release that trusts the fixture key (the repository keys are the publisher's)."""
    return functools.partial(audit_module.audit_release, signing_keys=keys)
