// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The production [`Downloader`]: `ureq` over rustls (ring provider), trusting
//! the operating system's certificate store like the Python Forge did.
//! No OpenSSL or native-tls.
//!
//! Desktop uses the platform verifier. Android has no verifier reachable
//! from plain Rust, so it trusts the system CA folder Android's own TLS
//! stack reads: the updatable Conscrypt module's
//! `/apex/com.android.conscrypt/cacerts` (Android 14+), else
//! `/system/etc/security/cacerts`. CAs the user added are not trusted, as
//! for any app that does not opt in; a CA the user disabled in the system
//! settings still is (that list is private to the system).

use std::sync::Arc;

use ureq::tls::{RootCerts, TlsConfig, TlsProvider};

use crate::download::{Downloader, HttpError, HttpRequest, HttpResponse};

#[derive(Clone, Copy, Debug, Default)]
pub struct UreqDownloader;

#[cfg(not(target_os = "android"))]
fn root_certs() -> std::result::Result<RootCerts, HttpError> {
    Ok(RootCerts::PlatformVerifier)
}

/// Android's system CA folders, newest first.
#[cfg(target_os = "android")]
const ANDROID_CA_FOLDERS: [&str; 2] = ["/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"];

#[cfg(target_os = "android")]
fn root_certs() -> std::result::Result<RootCerts, HttpError> {
    static ROOTS: std::sync::OnceLock<Arc<Vec<ureq::tls::Certificate<'static>>>> = std::sync::OnceLock::new();
    let roots = ROOTS.get_or_init(|| {
        let folders: Vec<&std::path::Path> = ANDROID_CA_FOLDERS.iter().map(std::path::Path::new).collect();
        Arc::new(first_certificate_folder(&folders))
    });
    if roots.is_empty() {
        return Err(HttpError::Transport("Android's system certificate store cannot be read".to_string()));
    }
    Ok(RootCerts::Specific(Arc::clone(roots)))
}

/// The certificates of the first folder that holds any: every file's first
/// PEM certificate (Android's files add a text dump after it). Unreadable
/// files are skipped.
#[cfg_attr(not(any(target_os = "android", test)), allow(dead_code))]
fn first_certificate_folder(folders: &[&std::path::Path]) -> Vec<ureq::tls::Certificate<'static>> {
    for folder in folders {
        let Ok(entries) = std::fs::read_dir(folder) else { continue };
        let mut paths: Vec<_> = entries.filter_map(|entry| entry.ok()).map(|entry| entry.path()).collect();
        paths.sort();
        let certificates: Vec<_> = paths
            .iter()
            .filter(|path| path.is_file())
            .filter_map(|path| std::fs::read(path).ok())
            .filter_map(|bytes| ureq::tls::Certificate::from_pem(&bytes).ok())
            .collect();
        if !certificates.is_empty() {
            return certificates;
        }
    }
    Vec::new()
}

fn agent(request: &HttpRequest<'_>) -> std::result::Result<ureq::Agent, HttpError> {
    let tls = TlsConfig::builder()
        .provider(TlsProvider::Rustls)
        .unversioned_rustls_crypto_provider(Arc::new(rustls::crypto::ring::default_provider()))
        .root_certs(root_certs()?)
        .build();
    let remaining = request.deadline.map(|deadline| deadline.saturating_duration_since(std::time::Instant::now()));
    Ok(ureq::Agent::config_builder()
        .tls_config(tls)
        .https_only(true)
        .user_agent(request.user_agent)
        .timeout_connect(Some(request.timeout))
        .timeout_recv_response(Some(request.timeout))
        .timeout_per_read(Some(request.timeout))
        .timeout_global(remaining)
        .build()
        .new_agent())
}

impl Downloader for UreqDownloader {
    fn open(&self, request: &HttpRequest<'_>) -> std::result::Result<HttpResponse, HttpError> {
        use ureq::ResponseExt;
        let response = agent(request)?.get(request.url).call().map_err(|error| match error {
            ureq::Error::StatusCode(code) => HttpError::Status { code, message: String::new() },
            other => HttpError::Transport(other.to_string()),
        })?;
        let final_url = response.get_uri().to_string();
        Ok(HttpResponse { final_url, body: Box::new(response.into_body().into_reader()) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const PEM: &str = "-----BEGIN CERTIFICATE-----\nAQIDBA==\n-----END CERTIFICATE-----\n";

    #[test]
    fn certificates_come_from_the_first_folder_that_has_any() {
        let root = tempfile::tempdir().unwrap();
        let empty = root.path().join("apex");
        let system = root.path().join("system");
        std::fs::create_dir_all(&empty).unwrap();
        std::fs::create_dir_all(&system).unwrap();
        std::fs::write(empty.join("notes.txt"), "no certificate here").unwrap();
        // Android's files: the PEM block, then a text dump of the certificate.
        std::fs::write(system.join("1a2b3c4d.0"), format!("{PEM}Certificate:\n    Data: ...\n")).unwrap();
        std::fs::write(system.join("5e6f7a8b.0"), PEM).unwrap();
        let missing = root.path().join("missing");
        let found = first_certificate_folder(&[&missing, &empty, &system]);
        assert_eq!(found.len(), 2);
        assert_eq!(found[0].der(), &[1, 2, 3, 4]);
        assert!(first_certificate_folder(&[&missing, &empty]).is_empty());
    }
}
