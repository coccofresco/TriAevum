// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The window's texts, as the Python Forge GUI wrote them (`forge_gui.py`).

use forge_mods::{ArchiveContract, RomFsMod, Status, UiTexturePack};

pub const DIALOG_TITLE: &str = "TriAevum Forge";
pub const WINDOW_TITLE: &str = "TriAevum Forge \u{2014} Echoes of Time";
pub const SUBTITLE: &str = "Select your own decrypted Nintendo 3DS cartridge image. Forge accepts .3ds and .cci \
ROMs, extracts the required native data, and does not handle encryption keys. You can also select a folder \
containing decompressed code.bin, exheader.bin and romfs.bin.";
pub const NO_LANGUAGES: &str = "Available after ROM preparation";
pub const LANGUAGE_SAVED: &str = "Game language saved. Applies on the next full game start.";
pub const CHECKING_INSTALLATION: &str = "Checking the local installation...";
pub const NO_ACTIVE_TITLE: &str = "No active title is installed. Select a decrypted ROM.";
pub const STARTING: &str = "Starting title preparation...";
pub const DOWNLOADING_HD_FIRST: &str = "Downloading the HD textures before the title preparation...";
pub const DOWNLOADING_MOD_FIRST: &str = "Downloading the enabled mod before the title preparation...";
pub const PREPARATION_FAILED: &str = "Title preparation failed.";
pub const READY_DIALOG: &str = "The title is ready. You can launch TriAevum \u{2014} Echoes of Time now.";
pub const CLOSE_WHILE_BUSY: &str = "Title preparation is still running. Close Forge?";
pub const NOT_AVAILABLE: &str = "Not available for the supported game revisions of this package.";
pub const HD_AUTO_DOWNLOAD: &str = "Download the HD textures with \"Prepare and install\" when they are not stored yet";
pub const HD_DOWNLOAD_FAILED_PREFIX: &str = "HD texture download failed";
pub const MOD_DOWNLOAD_FAILED_PREFIX: &str = "Mod download failed";
/// Rust addition: stored mods are verified off the window thread.
pub const CHECKING_STORED: &str = "Checking the stored files...";

pub fn ready_to_play(recipe: &str, directory: &std::path::Path) -> String {
    format!("Ready to play: {recipe}\n{}", directory.display())
}

pub fn private_data(data_root: &std::path::Path) -> String {
    format!("Private generated data: {}", data_root.display())
}

fn credit_names<'a>(credits: impl Iterator<Item = &'a forge_mods::Credit>, group: &str) -> String {
    credits.filter(|credit| credit.group == group).map(|credit| credit.name.as_str()).collect::<Vec<_>>().join(", ")
}

pub fn hd_description(pack: &UiTexturePack) -> String {
    format!(
        "{} {} by {} (credits: {}): HD textures for the HUD, the menus and TopScreen, in the game language you \
         choose here or in game. Licensed {}: TriAevum does not distribute it. Forge downloads the author's \
         original archive from GameBanana, verifies it and keeps it in your private data folder. If the download \
         does not work, download the zip from the mod page and select it here. Thanks to RLG (M-1) for \
         SingleScreen, the mod TriAevum's TopScreen is based on; its HD font is included with TriAevum.",
        pack.title,
        pack.version,
        pack.author,
        credit_names(pack.credits.iter(), "Key Authors"),
        pack.license_name
    )
}

pub fn mod_description(mod_: &RomFsMod) -> String {
    format!(
        "{} {} by {}: higher-fidelity models for Link and his items. Testing: {}. Tools: {}. Licensed {}: \
         TriAevum does not distribute it. Forge downloads the author's original archive from GameBanana, verifies \
         it and applies it to your local game data only. If the download does not work, download the archive from \
         the mod page and select it here.",
        mod_.title,
        mod_.version,
        mod_.author,
        credit_names(mod_.credits.iter(), "Testing"),
        credit_names(mod_.credits.iter(), "Tool Creation"),
        mod_.license_name
    )
}

pub fn mod_checkbox(mod_: &RomFsMod) -> String {
    format!("Use {} (applied by \"Prepare and install\")", mod_.title)
}

/// A stored-file check: not finished yet, finished, or failed.
pub type Checked = Option<Result<Status, String>>;

/// `_refresh_hd`.
pub fn hd_status(contract: Option<&ArchiveContract>, state: &Checked, auto_download: bool) -> String {
    let Some(contract) = contract else {
        return NOT_AVAILABLE.to_string();
    };
    match state {
        None => CHECKING_STORED.to_string(),
        Some(Err(error)) => format!("Cannot check the HD textures: {error}"),
        Some(Ok(Status::Ready)) => {
            "HD textures ready. They are applied when you prepare and install the title.".to_string()
        }
        Some(Ok(Status::Archive)) => {
            "HD texture archive stored. It is prepared and applied when you prepare and install the title.".to_string()
        }
        Some(Ok(Status::Missing)) if auto_download => format!(
            "Not stored yet: the HD textures ({} MB) are downloaded when you prepare and install the title. \
             Without them the game uses the native textures.",
            contract.bytes / 1_000_000
        ),
        Some(Ok(Status::Missing)) => "Not installed: the game uses the native textures.".to_string(),
    }
}

/// `_refresh_mod`.
pub fn mod_status(
    settings_error: Option<&str>,
    contract: Option<&ArchiveContract>,
    state: &Checked,
    enabled: bool,
) -> String {
    if let Some(error) = settings_error {
        return format!("Cannot read the mod settings: {error}");
    }
    if contract.is_none() {
        return NOT_AVAILABLE.to_string();
    }
    match state {
        None => CHECKING_STORED.to_string(),
        Some(Err(error)) => format!("Cannot check the mod files: {error}"),
        Some(Ok(Status::Missing)) if enabled => {
            "Not downloaded. It is downloaded when you prepare and install the title.".to_string()
        }
        Some(Ok(Status::Missing)) => "Off: the game uses its native models.".to_string(),
        Some(Ok(_)) if enabled => "On: applied when you prepare and install the title.".to_string(),
        Some(Ok(_)) => "Off: the game uses its native models. The downloaded archive is kept.".to_string(),
    }
}

pub const APPLY_MOD_CHANGE: &str = " Prepare and install the title again to apply the change.";

pub fn hd_download_failed(error: &str) -> String {
    format!(
        "{HD_DOWNLOAD_FAILED_PREFIX}: {error} The title is prepared with the native textures. Download the zip \
         from the mod page and select it with \"Select .zip...\", then prepare and install again."
    )
}

pub fn mod_download_failed(error: &str) -> String {
    format!(
        "{MOD_DOWNLOAD_FAILED_PREFIX}: {error} The title is prepared with the native models. Download the archive \
         from the mod page and select it with \"Select .rar...\"."
    )
}

pub fn manual_hd_failure(error: &str, contract: &ArchiveContract, pack: &UiTexturePack) -> String {
    format!(
        "{error}\n\nDownload {} from the mod page ({}) and select it with \"Select .zip...\".",
        contract.name, pack.page
    )
}

pub fn manual_mod_failure(error: &str, contract: &ArchiveContract, mod_: &RomFsMod) -> String {
    format!(
        "{error}\n\nDownload {} from the mod page ({}) and select it with \"Select .rar...\".",
        contract.name, mod_.page
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn contract() -> ArchiveContract {
        ArchiveContract { kind: "k".into(), name: "pack.zip".into(), bytes: 451_655_008, sha256: "0".into() }
    }

    #[test]
    fn hd_status_follows_the_python_window() {
        let contract = contract();
        assert_eq!(hd_status(None, &Some(Ok(Status::Ready)), true), NOT_AVAILABLE);
        assert_eq!(
            hd_status(Some(&contract), &Some(Ok(Status::Missing)), true),
            "Not stored yet: the HD textures (451 MB) are downloaded when you prepare and install the title. \
             Without them the game uses the native textures."
        );
        assert_eq!(
            hd_status(Some(&contract), &Some(Ok(Status::Missing)), false),
            "Not installed: the game uses the native textures."
        );
        assert!(hd_status(Some(&contract), &Some(Ok(Status::Archive)), false).starts_with("HD texture archive stored."));
    }

    #[test]
    fn mod_status_follows_the_python_window() {
        let contract = contract();
        let missing = Some(Ok(Status::Missing));
        assert_eq!(mod_status(Some("bad"), Some(&contract), &missing, true), "Cannot read the mod settings: bad");
        assert_eq!(mod_status(None, Some(&contract), &missing, false), "Off: the game uses its native models.");
        assert_eq!(
            mod_status(None, Some(&contract), &missing, true),
            "Not downloaded. It is downloaded when you prepare and install the title."
        );
        assert_eq!(
            mod_status(None, Some(&contract), &Some(Ok(Status::Ready)), false),
            "Off: the game uses its native models. The downloaded archive is kept."
        );
    }

    #[test]
    fn descriptions_name_the_credited_people() {
        let pack = UiTexturePack::vanilla_ui_hud();
        assert!(hd_description(&pack).starts_with(
            "OoT3D Vanilla UI/HUD 3.2 by chubbycubby (credits: HenrikoMagnifico, M-1): HD textures for the HUD"
        ));
        let mod_ = RomFsMod::vanilla_plus_link();
        assert!(mod_description(&mod_).contains("Testing: Fado, M-1, seiunsky_1084. Tools: Jameriquiah."));
        assert_eq!(
            hd_download_failed("boom."),
            "HD texture download failed: boom. The title is prepared with the native textures. Download the zip \
             from the mod page and select it with \"Select .zip...\", then prepare and install again."
        );
    }
}
