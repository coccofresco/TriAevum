// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The catalogued optional downloads: the Vanilla+ Link RomFS mod and the
//! Vanilla UI/HUD texture pack (`romfs_mods.VANILLA_PLUS_LINK`,
//! `ui_texture_pack.VANILLA_UI_HUD`). Identity, credits, licence and the exact
//! member allowlists are data contracts; tests substitute small ones.

use crate::{vanilla_plus_link_members, vanilla_ui_hud_members};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Credit {
    pub group: String,
    pub name: String,
    pub role: String,
    pub profile: String,
}

fn credit(group: &str, name: &str, role: &str, profile: &str) -> Credit {
    Credit { group: group.into(), name: name.into(), role: role.into(), profile: profile.into() }
}

/// One extractable file: its path, exact byte count and SHA-256.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Member {
    pub path: String,
    pub bytes: u64,
    pub sha256: String,
}

impl Member {
    pub fn new(path: impl Into<String>, bytes: u64, sha256: impl Into<String>) -> Self {
        Self { path: path.into(), bytes, sha256: sha256.into() }
    }
}

fn members_of(table: &[(&str, u64, &str)]) -> Vec<Member> {
    table.iter().map(|&(path, bytes, sha256)| Member::new(path, bytes, sha256)).collect()
}

/// What the archive functions need of a catalogued mod or texture pack
/// (`romfs_mods.CataloguedArchive`).
pub trait CataloguedArchive {
    fn archive_kind(&self) -> &str;
    fn title(&self) -> &str;
    fn version(&self) -> &str;
    fn downloads(&self) -> &[String];
}

/// GameBanana's file link for link_remodel_vanilla_plus_1_9.rar.
pub const VANILLA_PLUS_LINK_DOWNLOAD: &str = "https://gamebanana.com/dl/1827260";
/// The verbatim mirror of the same archive. It stays unused until the
/// maintainer approves it; there is no generic mirror fallback.
pub const VANILLA_PLUS_LINK_MIRROR_URL: &str = "https://github.com/coccofresco/triaevum-mod-mirror/releases/download/\
vanilla-plus-link-1.9/link_remodel_vanilla_plus_1_9.rar";
pub const VANILLA_PLUS_LINK_MIRROR_APPROVED: bool = false;
/// GameBanana's file link for oot3d_vanilla_ui_hud_32.zip.
pub const VANILLA_UI_HUD_DOWNLOAD: &str = "https://gamebanana.com/dl/1857740";

/// A catalogued RomFS mod. Archive members are RomFS paths (the archive root
/// is the RomFS root).
#[derive(Clone, Debug)]
pub struct RomFsMod {
    pub mod_id: String,
    pub archive_kind: String,
    pub title: String,
    pub version: String,
    pub author: String,
    pub page: String,
    pub downloads: Vec<String>,
    pub license_name: String,
    pub license_url: String,
    pub credits: Vec<Credit>,
    pub members: Vec<Member>,
}

impl RomFsMod {
    pub fn attribution(&self) -> String {
        format!("{} {} by {} ({}), {}", self.title, self.version, self.author, self.page, self.license_name)
    }

    /// Vanilla+ - Link Remodel And HD Item Models 1.9 (HugAMuffin).
    pub fn vanilla_plus_link() -> Self {
        let mut downloads = vec![VANILLA_PLUS_LINK_DOWNLOAD.to_string()];
        if VANILLA_PLUS_LINK_MIRROR_APPROVED {
            downloads.push(VANILLA_PLUS_LINK_MIRROR_URL.to_string());
        }
        Self {
            mod_id: "vanilla_plus_link".into(),
            archive_kind: "vanilla_plus_link_1_9_archive".into(),
            title: "Vanilla+ - Link Remodel And HD Item Models".into(),
            version: "1.9".into(),
            author: "HugAMuffin".into(),
            page: "https://gamebanana.com/mods/705890".into(),
            downloads,
            license_name: "CC BY-NC-ND 4.0".into(),
            license_url: "https://creativecommons.org/licenses/by-nc-nd/4.0/".into(),
            credits: vec![
                credit("Key Authors", "HugAMuffin", "3D Artists", "https://gamebanana.com/members/1608504"),
                credit("Testing", "Fado", "QA Lead", "https://gamebanana.com/members/1713801"),
                credit("Testing", "M-1", "Hardware Testing", "https://gamebanana.com/members/1544353"),
                credit("Testing", "seiunsky_1084", "Hardware Testing", "https://gamebanana.com/members/3964924"),
                credit("Tool Creation", "Jameriquiah", "Code Wizard", "https://gamebanana.com/members/1664482"),
            ],
            members: members_of(vanilla_plus_link_members::MEMBERS),
        }
    }
}

impl CataloguedArchive for RomFsMod {
    fn archive_kind(&self) -> &str {
        &self.archive_kind
    }
    fn title(&self) -> &str {
        &self.title
    }
    fn version(&self) -> &str {
        &self.version
    }
    fn downloads(&self) -> &[String] {
        &self.downloads
    }
}

/// The catalogued mods in catalogue order (`romfs_mods.CATALOG`). Apply order
/// and settings order follow it.
#[derive(Clone, Debug)]
pub struct Catalog {
    pub mods: Vec<RomFsMod>,
}

impl Catalog {
    pub fn builtin() -> Self {
        Self { mods: vec![RomFsMod::vanilla_plus_link()] }
    }

    pub fn get(&self, mod_id: &str) -> Option<&RomFsMod> {
        self.mods.iter().find(|item| item.mod_id == mod_id)
    }
}

/// The title's Azahar texture folder inside the archive (the USA title ID;
/// the runtime matches textures by content hash, whatever the region).
pub const TEXTURE_FOLDER: &str = "load/textures/0004000000033500";
/// The native menu language folders (rom:/menu/<folder>/) and the pack's
/// per-language family folders.
pub const LANGUAGE_FOLDERS: [&str; 13] = [
    "00_JP_JAPANESE",
    "01_US_ENGLISH",
    "02_EU_ENGLISH",
    "03_EU_GERMAN",
    "04_EU_FRENCH",
    "05_US_FRENCH",
    "06_EU_SPANISH",
    "07_US_SPANISH",
    "08_EU_ITALIAN",
    "GERMAN",
    "SPANISH",
    "ITALIAN",
    "JAPANESE",
];
pub const UI_ATLAS_HD_TEXTURE: &str = "UI/tex1_512x256_1FAF9B5B2ADF6038_13_mip0.png";

/// A catalogued texture pack. Members are paths below [`TEXTURE_FOLDER`]
/// inside the archive's root folder.
#[derive(Clone, Debug)]
pub struct UiTexturePack {
    pub mod_id: String,
    pub archive_kind: String,
    pub title: String,
    pub version: String,
    pub author: String,
    pub page: String,
    pub downloads: Vec<String>,
    pub license_name: String,
    pub license_url: String,
    pub credits: Vec<Credit>,
    pub archive_root: String,
    pub members: Vec<Member>,
}

impl UiTexturePack {
    pub fn attribution(&self) -> String {
        format!("{} {} by {} ({}), {}", self.title, self.version, self.author, self.page, self.license_name)
    }

    /// OoT3D Vanilla UI/HUD 3.2 (chubbycubby).
    pub fn vanilla_ui_hud() -> Self {
        Self {
            mod_id: "vanilla_ui_hud".into(),
            archive_kind: "vanilla_ui_hud_3_2_archive".into(),
            title: "OoT3D Vanilla UI/HUD".into(),
            version: "3.2".into(),
            author: "chubbycubby".into(),
            page: "https://gamebanana.com/mods/701929".into(),
            downloads: vec![VANILLA_UI_HUD_DOWNLOAD.into()],
            license_name: "CC BY-NC-ND 4.0".into(),
            license_url: "https://creativecommons.org/licenses/by-nc-nd/4.0/".into(),
            credits: vec![
                credit("Submitter", "chubbycubby", "", "https://gamebanana.com/members/5093056"),
                credit("Key Authors", "HenrikoMagnifico", "", "https://gamebanana.com/members/1655768"),
                credit("Key Authors", "M-1", "", "https://gamebanana.com/members/1544353"),
            ],
            // The archive's root folder spells "UI/HUD" with U+2044 FRACTION SLASH.
            archive_root: "OoT3D Vanilla UI\u{2044}HUD 3.2".into(),
            members: members_of(vanilla_ui_hud_members::MEMBERS),
        }
    }
}

impl CataloguedArchive for UiTexturePack {
    fn archive_kind(&self) -> &str {
        &self.archive_kind
    }
    fn title(&self) -> &str {
        &self.title
    }
    fn version(&self) -> &str {
        &self.version
    }
    fn downloads(&self) -> &[String] {
        &self.downloads
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn catalogue_matches_the_python_contract() {
        let mod_ = RomFsMod::vanilla_plus_link();
        assert_eq!(mod_.members.len(), 26);
        assert_eq!(mod_.downloads, [VANILLA_PLUS_LINK_DOWNLOAD]);
        assert_eq!(
            mod_.credits.iter().map(|c| c.name.as_str()).collect::<Vec<_>>(),
            ["HugAMuffin", "Fado", "M-1", "seiunsky_1084", "Jameriquiah"]
        );
        assert!(mod_.members.iter().all(|m| m.path.starts_with("actor/") || m.path.starts_with("misc/")));
        assert_eq!(
            mod_.attribution(),
            "Vanilla+ - Link Remodel And HD Item Models 1.9 by HugAMuffin (https://gamebanana.com/mods/705890), CC BY-NC-ND 4.0"
        );
        let pack = UiTexturePack::vanilla_ui_hud();
        assert_eq!(pack.members.len(), 76);
        assert!(pack.members.iter().any(|m| m.path == UI_ATLAS_HD_TEXTURE));
        assert_eq!(Catalog::builtin().get("vanilla_plus_link").unwrap().version, "1.9");
        // Every catalogued folder below UI/ is a known language folder.
        for member in &pack.members {
            let parts: Vec<&str> = member.path.split('/').collect();
            if parts.len() == 3 {
                assert!(LANGUAGE_FOLDERS.contains(&parts[1]), "{}", member.path);
            }
        }
    }
}
