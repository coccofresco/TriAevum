// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Optional HD UI texture pack and RomFS mods with verified downloads.
//!
//! Port of the Python Forge's `ui_texture_pack.py`, `romfs_mods.py` and
//! `verified_download.py` (tools/triaevum_release/), byte-compatible per
//! FORGE_SPEC.md: prepared folder layouts, `source.json` and `import.json`
//! receipts, `romfs_mods.json` settings and the O3RO overlay.
//!
//! Python's ambient state becomes parameters: the mod [`Catalog`], the
//! [`ArchiveExtractor`] (system bsdtar on desktop) and the [`Downloader`]
//! transport (rustls-based `UreqDownloader` behind the default `https`
//! feature). Python's `ValueError`/`OSError` split survives where behavior
//! depends on it: unavailable or inapplicable mods are reported through the
//! `report` callback and left out, malformed settings and OS failures are
//! errors, cancellation always propagates.
//!
//! Deliberate differences from Python, all in error handling or transport:
//! the catalogued downloader requires HTTPS for the URL and the final redirect
//! target; `import_archive` verifies the copy by hashing while copying and does
//! not expand `~`; ZIP members using a method this build cannot read are
//! reported with the "Select extracted folder..." hint (Python raised an
//! unhandled `NotImplementedError`); JSON parse diagnostics in settings errors
//! come from serde_json; the transport is rustls with the operating system's
//! certificate store instead of OpenSSL plus certifi.

pub mod archive;
pub mod catalog;
pub mod download;
pub mod extract;
#[cfg(feature = "https")]
pub mod http;
pub mod overlay;
pub mod romfs_mods;
pub mod ui_pack;
mod util;
mod vanilla_plus_link_members;
mod vanilla_ui_hud_members;

pub use archive::{
    archive_contract, archive_contract_from_file, archive_verified, downloads_directory, import_archive,
    stored_archive, ArchiveContract,
};
pub use catalog::{Catalog, CataloguedArchive, Credit, Member, RomFsMod, UiTexturePack};
pub use download::{
    download_archive, download_verified, Downloader, HttpError, HttpRequest, HttpResponse, VerifiedDownload,
};
pub use extract::{find_bsdtar, ArchiveExtractor, SystemBsdtar};
#[cfg(feature = "https")]
pub use http::UreqDownloader;
pub use overlay::{
    build_romfs_overlay, prepare_romfs_mods, romfs_file_records, romfs_service_view, OverlaySummary, PrepareContext,
    RomfsRecords,
};
pub use romfs_mods::{enabled_mods, mod_status, set_mod_enabled, stored_members, Status, StoredMembers};
pub use ui_pack::{pack_status, prepare_ui_texture_pack, prepared_pack, ui_atlas_hd};
