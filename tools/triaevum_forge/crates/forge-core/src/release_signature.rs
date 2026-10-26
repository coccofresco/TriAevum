// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Publisher signatures on update packages (owner decision 10).
//!
//! The release manifest already hashes every package file, which proves the
//! package is internally consistent, not who made it. The publisher signs
//! the manifest's exact bytes with Ed25519 (`ring`, the same implementation
//! the HTTPS client uses) and ships the detached signature beside it:
//! `docs/release-signature.json` on Windows, `release-signature.json`
//! elsewhere:
//!
//! ```json
//! {"format": "triaevum_release_signature_v1", "algorithm": "ed25519",
//!  "key_id": "<16 hex>", "manifest_sha256": "<64 hex>", "signature": "<128 hex>"}
//! ```
//!
//! The signed message is [`DOMAIN`] followed by the manifest bytes, so a
//! signature cannot be replayed for another purpose. Forge embeds the
//! public keys it trusts (`tools/triaevum_forge/update-signing-keys.json`,
//! a list so keys can rotate); the private key never enters the repository
//! or a package (`tools/triaevum_release/release_signature.py`,
//! `docs/TRIAEVUM_UPDATE_SIGNING.md`).
//!
//! An unsigned package, or one signed by a key this Forge does not know, is
//! refused. A developer may override that only with both
//! [`OVERRIDE_VARIABLE`] set to [`OVERRIDE_VALUE`] and the request's explicit
//! flag (`--allow-unsigned-developer-package`); the result then says so. A
//! signature that does not verify under a trusted key is always refused.

use forge_base::hashing::{hex, is_sha256_hex, sha256_hex};
use forge_base::{ErrorKind, ForgeError, Operation, Reason, Result, Subject};
use ring::signature::KeyPair;
use serde_json::{json, Map, Value};

pub const SIGNATURE_FORMAT: &str = "triaevum_release_signature_v1";
pub const KEYS_FORMAT: &str = "triaevum_update_signing_keys_v1";
pub use crate::package_inventory::SIGNATURE_NAME;
pub const DOMAIN: &[u8] = b"TriAevum release manifest signature v1\n";
pub const OVERRIDE_VARIABLE: &str = "TRIAEVUM_FORGE_ALLOW_UNSIGNED_UPDATE";
pub const OVERRIDE_VALUE: &str = "unsigned-developer-package";
/// The bytes of a signature document are bounded before parsing.
pub const MAX_SIGNATURE_BYTES: u64 = 16 * 1024;

const EMBEDDED_KEYS: &str = include_str!("../../../update-signing-keys.json");

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TrustedKey {
    pub key_id: String,
    pub public_key: [u8; 32],
}

impl TrustedKey {
    pub fn new(public_key: [u8; 32]) -> Self {
        Self { key_id: key_id(&public_key), public_key }
    }
}

/// The first 16 hex digits of the public key's SHA-256.
pub fn key_id(public_key: &[u8; 32]) -> String {
    sha256_hex(public_key)[..16].to_string()
}

fn unhex<const N: usize>(text: &str) -> Option<[u8; N]> {
    if text.len() != N * 2 || !text.bytes().all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()) {
        return None;
    }
    let mut bytes = [0u8; N];
    for (index, byte) in bytes.iter_mut().enumerate() {
        *byte = u8::from_str_radix(&text[index * 2..index * 2 + 2], 16).ok()?;
    }
    Some(bytes)
}

/// Parses a key list document.
pub fn parse_keys(text: &str) -> Result<Vec<TrustedKey>> {
    let invalid = |message: &str| ForgeError::internal(format!("Invalid update signing keys: {message}"));
    let document: Value = serde_json::from_str(text).map_err(|error| invalid(&error.to_string()))?;
    if document.get("format").and_then(Value::as_str) != Some(KEYS_FORMAT) {
        return Err(invalid("unsupported format"));
    }
    let mut keys = Vec::new();
    for entry in document.get("keys").and_then(Value::as_array).ok_or_else(|| invalid("no key list"))? {
        let field = |key: &str| entry.get(key).and_then(Value::as_str).unwrap_or_default();
        if field("algorithm") != "ed25519" {
            return Err(invalid("only ed25519 keys are supported"));
        }
        let public_key = unhex::<32>(field("public_key")).ok_or_else(|| invalid("public_key must be 64 hex digits"))?;
        let key = TrustedKey::new(public_key);
        if field("key_id") != key.key_id {
            return Err(invalid("key_id does not match the public key"));
        }
        keys.push(key);
    }
    Ok(keys)
}

/// The publisher keys built into this Forge.
pub fn embedded_keys() -> Vec<TrustedKey> {
    parse_keys(EMBEDDED_KEYS).expect("the embedded update signing keys are valid (tested)")
}

/// What decides whether a package is authentic enough to stage.
#[derive(Clone, Debug)]
pub struct Trust {
    pub keys: Vec<TrustedKey>,
    /// The developer override is active (variable and flag both given).
    pub allow_unsigned: bool,
}

impl Trust {
    /// The embedded keys; the override when `flag` and the variable agree.
    pub fn publisher(flag: bool) -> Self {
        Self { keys: embedded_keys(), allow_unsigned: override_requested(flag) }
    }
}

/// The developer override needs the explicit flag and the exact variable.
pub fn override_requested(flag: bool) -> bool {
    flag && std::env::var(OVERRIDE_VARIABLE).is_ok_and(|value| value == OVERRIDE_VALUE)
}

/// How a staged package was found authentic.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Authenticity {
    Signed {
        key_id: String,
    },
    /// No signature, or one by an unknown key, accepted only through the
    /// developer override. `reason` names which.
    DeveloperOverride {
        reason: &'static str,
    },
}

impl Authenticity {
    pub fn to_value(&self) -> Value {
        match self {
            Self::Signed { key_id } => json!({"status": "signed", "key_id": key_id}),
            Self::DeveloperOverride { reason } => {
                json!({"status": "unsigned_developer_override", "reason": reason})
            }
        }
    }
}

fn refused(reason: Reason, message: impl Into<String>) -> ForgeError {
    ForgeError::new(ErrorKind::ArtifactIntegrity, message)
        .during(Operation::PackageVerify)
        .about(Subject::Package)
        .because(reason)
}

/// Verifies the signature document over the manifest bytes.
pub fn verify(manifest: &[u8], signature: Option<&[u8]>, trust: &Trust) -> Result<Authenticity> {
    let unverified = |reason: Reason, code: &'static str, message: String| {
        if trust.allow_unsigned {
            Ok(Authenticity::DeveloperOverride { reason: code })
        } else {
            Err(refused(reason, message))
        }
    };
    let Some(signature) = signature else {
        return unverified(
            Reason::UnsignedPackage,
            "unsigned",
            "This package has no publisher signature, so Forge cannot tell who made it".into(),
        );
    };
    let document: Map<String, Value> = match serde_json::from_slice(signature) {
        Ok(Value::Object(document)) => document,
        _ => return Err(refused(Reason::InvalidSignature, "The package's signature file is not valid")),
    };
    let field = |key: &str| document.get(key).and_then(Value::as_str).unwrap_or_default();
    if field("format") != SIGNATURE_FORMAT || field("algorithm") != "ed25519" {
        return Err(refused(Reason::InvalidSignature, "The package's signature has an unsupported format"));
    }
    let digest = field("manifest_sha256");
    if !is_sha256_hex(digest) || digest != sha256_hex(manifest) {
        return Err(refused(Reason::InvalidSignature, "The package's signature belongs to another release manifest"));
    }
    let Some(bytes) = unhex::<64>(field("signature")) else {
        return Err(refused(Reason::InvalidSignature, "The package's signature is malformed"));
    };
    let Some(key) = trust.keys.iter().find(|key| key.key_id == field("key_id")) else {
        return unverified(
            Reason::InvalidSignature,
            "unknown_key",
            format!("This package is signed with a key this Forge does not trust ({})", field("key_id")),
        );
    };
    let mut message = DOMAIN.to_vec();
    message.extend_from_slice(manifest);
    ring::signature::UnparsedPublicKey::new(&ring::signature::ED25519, key.public_key)
        .verify(&message, &bytes)
        .map_err(|_| refused(Reason::InvalidSignature, "The package's publisher signature does not verify"))?;
    Ok(Authenticity::Signed { key_id: key.key_id.clone() })
}

/// Signs `manifest` with a PKCS#8 Ed25519 key: the signature document.
/// The release tooling signs in Python; this serves tests and tools.
pub fn sign(manifest: &[u8], pkcs8: &[u8]) -> Result<Value> {
    let pair = ring::signature::Ed25519KeyPair::from_pkcs8_maybe_unchecked(pkcs8)
        .map_err(|_| ForgeError::invalid("Not a PKCS#8 Ed25519 private key"))?;
    let public: [u8; 32] = pair.public_key().as_ref().try_into().expect("Ed25519 public keys have 32 bytes");
    let mut message = DOMAIN.to_vec();
    message.extend_from_slice(manifest);
    Ok(json!({
        "format": SIGNATURE_FORMAT,
        "algorithm": "ed25519",
        "key_id": key_id(&public),
        "manifest_sha256": sha256_hex(manifest),
        "signature": hex(pair.sign(&message).as_ref()),
    }))
}

/// A throwaway key pair for tests: (PKCS#8 bytes, public key).
pub fn generate_test_key() -> (Vec<u8>, TrustedKey) {
    let random = ring::rand::SystemRandom::new();
    let document = ring::signature::Ed25519KeyPair::generate_pkcs8(&random).expect("system randomness");
    let pair = ring::signature::Ed25519KeyPair::from_pkcs8(document.as_ref()).expect("generated key");
    let public: [u8; 32] = pair.public_key().as_ref().try_into().expect("32-byte key");
    (document.as_ref().to_vec(), TrustedKey::new(public))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_embedded_key_list_is_valid() {
        parse_keys(EMBEDDED_KEYS).unwrap();
    }

    #[test]
    fn a_signed_manifest_verifies_and_every_tampering_is_refused() {
        let (private, public) = generate_test_key();
        let manifest = br#"{"format": "triaevum_public_release_manifest_v1"}"#;
        let signature = serde_json::to_vec(&sign(manifest, &private).unwrap()).unwrap();
        let trust = Trust { keys: vec![public.clone()], allow_unsigned: false };
        assert_eq!(
            verify(manifest, Some(&signature), &trust).unwrap(),
            Authenticity::Signed { key_id: public.key_id.clone() }
        );
        // Another manifest, a flipped signature byte, an unknown key, none.
        let error = verify(b"{}", Some(&signature), &trust).unwrap_err();
        assert_eq!(error.reason, Some(Reason::InvalidSignature));
        let mut document: Value = serde_json::from_slice(&signature).unwrap();
        let text = document["signature"].as_str().unwrap().to_string();
        let flipped = format!("{}{}", if text.starts_with('0') { "1" } else { "0" }, &text[1..]);
        document["signature"] = json!(flipped);
        let tampered = serde_json::to_vec(&document).unwrap();
        let error = verify(manifest, Some(&tampered), &trust).unwrap_err();
        assert_eq!(error.message, "The package's publisher signature does not verify");
        let stranger = Trust { keys: vec![generate_test_key().1], allow_unsigned: false };
        assert_eq!(verify(manifest, Some(&signature), &stranger).unwrap_err().reason, Some(Reason::InvalidSignature));
        let error = verify(manifest, None, &trust).unwrap_err();
        assert_eq!((error.kind, error.reason), (ErrorKind::ArtifactIntegrity, Some(Reason::UnsignedPackage)));

        // The override accepts unsigned and unknown-key packages, never a
        // bad signature by a trusted key.
        let developer = Trust { keys: vec![public], allow_unsigned: true };
        assert_eq!(verify(manifest, None, &developer).unwrap(), Authenticity::DeveloperOverride { reason: "unsigned" });
        let developer_stranger = Trust { keys: Vec::new(), allow_unsigned: true };
        assert_eq!(
            verify(manifest, Some(&signature), &developer_stranger).unwrap().to_value()["reason"],
            "unknown_key"
        );
        assert!(verify(manifest, Some(&tampered), &developer).is_err());
    }

    #[test]
    fn the_override_needs_the_flag_and_the_exact_variable() {
        assert!(!override_requested(false));
        // The variable is not set by the test environment.
        if std::env::var(OVERRIDE_VARIABLE).is_err() {
            assert!(!override_requested(true));
        }
    }

    #[test]
    fn key_lists_bind_ids_to_keys() {
        let key = generate_test_key().1;
        let document = json!({"format": KEYS_FORMAT, "keys": [
            {"key_id": key.key_id, "algorithm": "ed25519", "public_key": hex(&key.public_key)}]});
        assert_eq!(parse_keys(&document.to_string()).unwrap(), vec![key.clone()]);
        let wrong = json!({"format": KEYS_FORMAT, "keys": [
            {"key_id": "0000000000000000", "algorithm": "ed25519", "public_key": hex(&key.public_key)}]});
        assert!(parse_keys(&wrong.to_string()).is_err());
    }
}
