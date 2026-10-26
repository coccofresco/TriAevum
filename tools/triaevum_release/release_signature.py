"""Publisher signatures of release packages (UX plan WP-04B, owner decision 10).

The release manifest hashes every package file, which proves a package is
consistent, not who made it. The publisher signs the manifest's exact bytes
with Ed25519 and ships the detached signature beside it
(`docs/release-signature.json` on Windows, `release-signature.json`
elsewhere); Forge refuses update packages without a signature from a key it
embeds (`tools/triaevum_forge/update-signing-keys.json`).

The private key never enters the repository or a package: it lives outside
the repository, named by `--key` or TRIAEVUM_RELEASE_SIGNING_KEY, optionally
encrypted with TRIAEVUM_RELEASE_SIGNING_PASSPHRASE. Publisher tooling; needs
the `cryptography` package (`pip install cryptography`).

    # once, by the owner (docs/TRIAEVUM_UPDATE_SIGNING.md):
    python tools/triaevum_release/release_signature.py generate-key --private-key D:/keys/triaevum-release.pem --add
    # each release, after package_release.py and before archive_release.py:
    python tools/triaevum_release/release_signature.py sign --package <package folder>
    python tools/triaevum_release/release_signature.py verify --package <package folder>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from typing import Any

try:
    from .common import release_manifest_path
except ImportError:
    from common import release_manifest_path


REPO_ROOT = Path(__file__).resolve().parents[2]
KEYS = REPO_ROOT / "tools/triaevum_forge/update-signing-keys.json"
FORMAT = "triaevum_release_signature_v1"
KEYS_FORMAT = "triaevum_update_signing_keys_v1"
NAME = "release-signature.json"
# Must match forge_core::release_signature::DOMAIN.
DOMAIN = b"TriAevum release manifest signature v1\n"
KEY_VARIABLE = "TRIAEVUM_RELEASE_SIGNING_KEY"
PASSPHRASE_VARIABLE = "TRIAEVUM_RELEASE_SIGNING_PASSPHRASE"
MAX_SIGNATURE_BYTES = 16 * 1024


class SigningError(ValueError):
    pass


def _ed25519():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ed25519
    except ImportError as exc:
        raise SigningError("release signing needs the cryptography package (pip install cryptography)") from exc
    return serialization, ed25519


def key_id(public_key: bytes) -> str:
    return hashlib.sha256(public_key).hexdigest()[:16]


def signature_path(manifest_path: Path) -> Path:
    return manifest_path.with_name(NAME)


def load_keys(path: Path = KEYS) -> dict[str, bytes]:
    """key_id -> raw Ed25519 public key, checked like Forge checks them."""
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("format") != KEYS_FORMAT or not isinstance(document.get("keys"), list):
        raise SigningError(f"{path} is not an update signing key list")
    keys = {}
    for entry in document["keys"]:
        public = bytes.fromhex(entry.get("public_key", ""))
        if entry.get("algorithm") != "ed25519" or len(public) != 32 or entry.get("key_id") != key_id(public):
            raise SigningError(f"invalid key entry in {path}: {entry.get('key_id')}")
        keys[entry["key_id"]] = public
    return keys


def _outside(path: Path, *roots: Path) -> None:
    resolved = path.resolve()
    for root in roots:
        if resolved.is_relative_to(root.resolve()):
            raise SigningError(f"the private key must stay outside {root}: {path}")


def _passphrase() -> bytes | None:
    value = os.environ.get(PASSPHRASE_VARIABLE)
    return value.encode("utf-8") if value else None


def generate_key(private_key: Path, comment: str = "") -> dict[str, Any]:
    """Writes a new private key outside the repository; returns the public
    key entry for update-signing-keys.json."""
    serialization, ed25519 = _ed25519()
    _outside(private_key, REPO_ROOT)
    if private_key.exists():
        raise SigningError(f"refusing to overwrite an existing key: {private_key}")
    key = ed25519.Ed25519PrivateKey.generate()
    passphrase = _passphrase()
    encryption = (serialization.BestAvailableEncryption(passphrase) if passphrase
                  else serialization.NoEncryption())
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, encryption)
    private_key.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(private_key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(pem)
    public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    return {"key_id": key_id(public), "algorithm": "ed25519", "public_key": public.hex(), "comment": comment}


def add_key(entry: dict[str, Any], path: Path = KEYS) -> None:
    document = json.loads(path.read_text(encoding="utf-8"))
    if any(existing.get("key_id") == entry["key_id"] for existing in document["keys"]):
        return
    document["keys"].append(entry)
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")
    load_keys(path)


def _private_key(path: Path):
    serialization, ed25519 = _ed25519()
    key = serialization.load_pem_private_key(path.read_bytes(), password=_passphrase())
    if not isinstance(key, ed25519.Ed25519PrivateKey):
        raise SigningError(f"{path} is not an Ed25519 private key")
    return key


def sign_manifest(manifest: bytes, private_key: Path) -> dict[str, Any]:
    serialization, _ = _ed25519()
    key = _private_key(private_key)
    public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    return {"format": FORMAT, "algorithm": "ed25519", "key_id": key_id(public),
            "manifest_sha256": hashlib.sha256(manifest).hexdigest(),
            "signature": key.sign(DOMAIN + manifest).hex()}


def signature_errors(manifest: bytes, signature: bytes, keys: dict[str, bytes]) -> list[str]:
    """Why `signature` is not a valid publisher signature of `manifest`."""
    _, ed25519 = _ed25519()
    if len(signature) > MAX_SIGNATURE_BYTES:
        return ["release signature is too large"]
    try:
        document = json.loads(signature)
    except ValueError:
        return ["release signature is not JSON"]
    if not isinstance(document, dict) or document.get("format") != FORMAT or document.get("algorithm") != "ed25519":
        return ["release signature has an unsupported format"]
    if document.get("manifest_sha256") != hashlib.sha256(manifest).hexdigest():
        return ["release signature belongs to another manifest"]
    public = keys.get(str(document.get("key_id")))
    if public is None:
        return [f"release signature key {document.get('key_id')} is not in update-signing-keys.json"]
    try:
        raw = bytes.fromhex(str(document.get("signature", "")))
        ed25519.Ed25519PublicKey.from_public_bytes(public).verify(raw, DOMAIN + manifest)
    except Exception:  # cryptography raises InvalidSignature; bad hex raises ValueError
        return ["release signature does not verify"]
    return []


def sign_package(package: Path, private_key: Path, keys_path: Path = KEYS) -> Path:
    """Signs the package's manifest; the key must be one Forge trusts."""
    package = package.resolve()
    _outside(private_key, REPO_ROOT, package)
    manifest_path = release_manifest_path(package)
    manifest = manifest_path.read_bytes()
    document = sign_manifest(manifest, private_key)
    if document["key_id"] not in load_keys(keys_path):
        raise SigningError(f"key {document['key_id']} is not in {keys_path}; add it and rebuild Forge first")
    target = signature_path(manifest_path)
    payload = json.dumps(document, indent=2).encode("utf-8") + b"\n"
    errors = signature_errors(manifest, payload, load_keys(keys_path))
    if errors:
        raise SigningError("; ".join(errors))
    target.write_bytes(payload)
    return target


def verify_package(package: Path, keys_path: Path = KEYS) -> list[str]:
    manifest_path = release_manifest_path(package.resolve())
    target = signature_path(manifest_path)
    if not target.is_file():
        return [f"the package is not signed ({target.name} is missing)"]
    return signature_errors(manifest_path.read_bytes(), target.read_bytes(), load_keys(keys_path))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate-key", help="create the publisher key (once, by the owner)")
    generate.add_argument("--private-key", type=Path, required=True, help="where to write it, outside the repository")
    generate.add_argument("--comment", default="TriAevum release key")
    generate.add_argument("--add", action="store_true", help="add the public key to update-signing-keys.json")
    sign = commands.add_parser("sign", help="sign a built package's release manifest")
    sign.add_argument("--package", type=Path, required=True)
    sign.add_argument("--key", type=Path, help=f"the private key (default: ${KEY_VARIABLE})")
    verify = commands.add_parser("verify", help="check a package's signature against the embedded keys")
    verify.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "generate-key":
            entry = generate_key(args.private_key, args.comment)
            if args.add:
                add_key(entry)
            print(json.dumps(entry, indent=2))
        elif args.command == "sign":
            key = args.key or (Path(os.environ[KEY_VARIABLE]) if os.environ.get(KEY_VARIABLE) else None)
            if key is None:
                parser.error(f"name the private key with --key or {KEY_VARIABLE}")
            print(sign_package(args.package, key))
        else:
            errors = verify_package(args.package)
            if errors:
                print("TriAevum release signature check failed: " + "; ".join(errors))
                return 1
            print("TriAevum release signature verified")
    except (OSError, ValueError) as exc:
        print(f"TriAevum release signing failed: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
