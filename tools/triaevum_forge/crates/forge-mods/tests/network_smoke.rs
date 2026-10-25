// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Manual smoke test of the rustls transport against a public HTTPS site
//! (`cargo test -p forge-mods --test network_smoke -- --ignored`). Never
//! downloads a catalogued archive.

#![cfg(feature = "https")]

use std::io::Read;
use std::time::Duration;

use forge_mods::{Downloader, HttpError, HttpRequest, UreqDownloader};

fn request(url: &str) -> HttpRequest<'_> {
    HttpRequest { url, user_agent: "TriAevum-Forge", timeout: Duration::from_secs(30), deadline: None }
}

#[test]
#[ignore = "needs network access"]
fn https_get_status_and_plain_http_refusal() {
    let mut response = UreqDownloader.open(&request("https://example.com/")).unwrap();
    assert!(response.final_url.starts_with("https://"));
    let mut body = String::new();
    response.body.read_to_string(&mut body).unwrap();
    assert!(body.contains("Example Domain"));
    match UreqDownloader.open(&request("https://example.com/definitely-missing-triaevum")) {
        Err(HttpError::Status { code, .. }) => assert_eq!(code, 404),
        other => panic!("expected a 404, got {:?}", other.map(|_| ())),
    }
    assert!(UreqDownloader.open(&request("http://example.com/")).is_err());
}
