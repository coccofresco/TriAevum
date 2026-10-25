// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The production [`Downloader`]: `ureq` over rustls (ring provider), trusting
//! the operating system's certificate store like the Python Forge did.
//! No OpenSSL or native-tls. Android has no system store reachable from plain
//! Rust: hosts there supply their own `Downloader`.

use std::sync::Arc;

use ureq::tls::{RootCerts, TlsConfig, TlsProvider};

use crate::download::{Downloader, HttpError, HttpRequest, HttpResponse};

#[derive(Clone, Copy, Debug, Default)]
pub struct UreqDownloader;

fn agent(request: &HttpRequest<'_>) -> ureq::Agent {
    let tls = TlsConfig::builder()
        .provider(TlsProvider::Rustls)
        .unversioned_rustls_crypto_provider(Arc::new(rustls::crypto::ring::default_provider()))
        .root_certs(RootCerts::PlatformVerifier)
        .build();
    let remaining = request.deadline.map(|deadline| deadline.saturating_duration_since(std::time::Instant::now()));
    ureq::Agent::config_builder()
        .tls_config(tls)
        .https_only(true)
        .user_agent(request.user_agent)
        .timeout_connect(Some(request.timeout))
        .timeout_recv_response(Some(request.timeout))
        .timeout_per_read(Some(request.timeout))
        .timeout_global(remaining)
        .build()
        .new_agent()
}

impl Downloader for UreqDownloader {
    fn open(&self, request: &HttpRequest<'_>) -> std::result::Result<HttpResponse, HttpError> {
        use ureq::ResponseExt;
        let response = agent(request).get(request.url).call().map_err(|error| match error {
            ureq::Error::StatusCode(code) => HttpError::Status { code, message: String::new() },
            other => HttpError::Transport(other.to_string()),
        })?;
        let final_url = response.get_uri().to_string();
        Ok(HttpResponse { final_url, body: Box::new(response.into_body().into_reader()) })
    }
}
