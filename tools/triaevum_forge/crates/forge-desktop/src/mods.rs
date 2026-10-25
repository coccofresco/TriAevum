// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The two optional downloads the window offers, the HD UI texture pack and
//! the catalogued RomFS mod, bound to this installation. Every call can take
//! seconds (hashing, copying, downloading): callers run them off the window
//! thread.

use std::path::{Path, PathBuf};

use forge_base::{CancelToken, Result};
use forge_mods::{archive_contract, ArchiveContract, Catalog, RomFsMod, Status, UiTexturePack, UreqDownloader};
use serde_json::Value;

use crate::installation::Installation;

pub struct Optional {
    pub root: PathBuf,
    pub data_root: PathBuf,
    pub catalog: Catalog,
    pub pack: UiTexturePack,
    pub mod_: RomFsMod,
    pub hd_contract: Option<ArchiveContract>,
    pub mod_contract: Option<ArchiveContract>,
}

impl Optional {
    /// Reads the recipes' archive contracts. Deliberate difference: the
    /// Python window did not open when the recipes could not be read; here
    /// both sections become unavailable and the error is returned for the
    /// status line, so the window can still explain the installation state.
    pub fn load(installation: &Installation) -> (Self, Option<String>) {
        let catalog = Catalog::builtin();
        let mod_ = catalog.get("vanilla_plus_link").cloned().unwrap_or_else(RomFsMod::vanilla_plus_link);
        let mut optional = Self {
            root: installation.root.clone(),
            data_root: installation.data_root.clone(),
            catalog,
            pack: UiTexturePack::vanilla_ui_hud(),
            mod_,
            hd_contract: None,
            mod_contract: None,
        };
        let contracts = forge_base::fsutil::load_json_object(&installation.recipes()).and_then(|recipes| {
            let recipes = Value::Object(recipes);
            Ok((archive_contract(&recipes, &optional.pack)?, archive_contract(&recipes, &optional.mod_)?))
        });
        match contracts {
            Ok((hd, mod_)) => {
                optional.hd_contract = hd;
                optional.mod_contract = mod_;
                (optional, None)
            }
            Err(error) => (optional, Some(error.message)),
        }
    }

    pub fn hd_state(&self, cancel: &CancelToken) -> std::result::Result<Status, String> {
        forge_mods::pack_status(&self.root, &self.data_root, self.hd_contract.as_ref(), &self.pack, cancel)
            .map_err(|error| error.message)
    }

    pub fn mod_state(&self, cancel: &CancelToken) -> std::result::Result<Status, String> {
        forge_mods::mod_status(&self.root, &self.data_root, &self.mod_, self.mod_contract.as_ref(), cancel)
            .map_err(|error| error.message)
    }

    pub fn enabled(&self) -> Result<bool> {
        Ok(forge_mods::enabled_mods(&self.data_root, &self.catalog)?.contains(&self.mod_.mod_id))
    }

    pub fn set_enabled(&self, enabled: bool) -> Result<()> {
        forge_mods::set_mod_enabled(&self.data_root, &self.catalog, &self.mod_.mod_id, enabled).map(drop)
    }

    pub fn download_hd(&self, report: &mut dyn FnMut(String), cancel: &CancelToken) -> Result<()> {
        let Some(contract) = &self.hd_contract else { return Ok(()) };
        download(&self.data_root, &self.pack, contract, "the HD textures", report, cancel)
    }

    pub fn download_mod(&self, report: &mut dyn FnMut(String), cancel: &CancelToken) -> Result<()> {
        let Some(contract) = &self.mod_contract else { return Ok(()) };
        let name = format!("{} {}", self.mod_.title, self.mod_.version);
        download(&self.data_root, &self.mod_, contract, &name, report, cancel)
    }

    pub fn import_hd_archive(&self, source: &Path, cancel: &CancelToken) -> Result<()> {
        let Some(contract) = &self.hd_contract else { return Ok(()) };
        forge_mods::import_archive(source, &self.data_root, &self.pack, contract, cancel).map(drop)
    }

    pub fn import_hd_folder(&self, folder: &Path, cancel: &CancelToken) -> Result<()> {
        forge_mods::ui_pack::import_extracted(folder, &self.data_root, &self.pack, cancel).map(drop)
    }

    pub fn import_mod_archive(&self, source: &Path, cancel: &CancelToken) -> Result<()> {
        let Some(contract) = &self.mod_contract else { return Ok(()) };
        forge_mods::import_archive(source, &self.data_root, &self.mod_, contract, cancel).map(drop)
    }

    pub fn import_mod_folder(&self, folder: &Path, cancel: &CancelToken) -> Result<()> {
        forge_mods::romfs_mods::import_extracted(folder, &self.data_root, &self.mod_, cancel).map(drop)
    }
}

/// `romfs_mods.download_archive` with the window's progress text, sent only
/// when the percentage changes.
fn download(
    data_root: &Path,
    archive: &dyn forge_mods::CataloguedArchive,
    contract: &ArchiveContract,
    name: &str,
    report: &mut dyn FnMut(String),
    cancel: &CancelToken,
) -> Result<()> {
    let report = std::cell::RefCell::new(report);
    let mut last = None;
    let mut progress = |done: u64, total: u64| {
        let percent = done * 100 / total.max(1);
        if last != Some(percent) {
            last = Some(percent);
            (report.borrow_mut())(format!("Downloading {name}... {percent}%"));
        }
    };
    let mut failure = |text: &str| (report.borrow_mut())(text.to_string());
    forge_mods::download_archive(
        data_root,
        archive,
        contract,
        None,
        &UreqDownloader,
        &mut progress,
        &mut failure,
        cancel,
    )
    .map(drop)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_recipes_leave_both_sections_unavailable() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        let (optional, error) = Optional::load(&installation);
        assert!(error.is_some());
        assert!(optional.hd_contract.is_none() && optional.mod_contract.is_none());
        assert_eq!(optional.hd_state(&CancelToken::new()), Ok(Status::Missing));
        assert!(!optional.enabled().unwrap());
    }

    #[test]
    fn recipe_contracts_are_read() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        std::fs::create_dir(installation.root.join("recipes")).unwrap();
        std::fs::write(
            installation.root.join("recipes").join("oot3d.json"),
            br#"{"format": "triaevum_supported_revisions_v1", "recipes": [{"id": "r", "optional_inputs": {
                "vanilla_ui_hud_3_2_archive": {"name": "pack.zip", "bytes": 3, "sha256": "AB"}}}]}"#,
        )
        .unwrap();
        let (optional, error) = Optional::load(&installation);
        assert_eq!(error, None);
        assert_eq!(optional.hd_contract.unwrap().sha256, "ab");
        assert!(optional.mod_contract.is_none());
    }
}
