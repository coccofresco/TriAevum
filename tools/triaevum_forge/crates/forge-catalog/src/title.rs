// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The Forge's product title, selected when Forge is BUILT (owner decision
//! 2026-11-08: Forge is per title). The OoT3D Forge (default) and the MM3D
//! Forge (`--features title-mm3d`) share this code, but each carries only its
//! own supported revisions, package identity and data namespace: neither
//! recognizes nor prepares the other title. A dump of another known title is
//! rejected with a diagnostic that names that title and its Forge.
//!
//! Title facts here are identities only (the EUR code.bin digests the
//! titles' recipes pin), used to name a foreign dump; recipes stay the data.

/// One product title's Forge identity.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ForgeTitle {
    /// Recipe `game` / title module id.
    pub id: &'static str,
    pub display_name: &'static str,
    /// The packaged supported revisions beside the installed Forge.
    pub packaged_recipes: &'static str,
    /// The source-tree supported revisions (developer fallback).
    pub embedded_recipes: &'static str,
    /// Per-user data folder name where a package cannot hold its data
    /// (AppImage/Flatpak); Windows keeps data in the package.
    pub data_namespace: &'static str,
    /// Product identity of the package (Flatpak id).
    pub package_identity: &'static str,
    /// The Forge's product name in diagnostics.
    pub forge_product: &'static str,
    /// Lower-case code.bin SHA-256 of the title's known revisions.
    pub code_sha256: &'static [&'static str],
    /// Whether the title's runtime adapter has the offline interior-sun
    /// prebuild (`--prebuild-interior-sun`, OoT3D's titles/oot3d/environment).
    /// A title without one installs with no interior-sun step.
    pub offline_interior_sun: bool,
}

pub const OOT3D: ForgeTitle = ForgeTitle {
    id: "oot3d",
    display_name: "The Legend of Zelda: Ocarina of Time 3D",
    packaged_recipes: "recipes/oot3d.json",
    embedded_recipes: "tools/triaevum_release/supported_revisions.json",
    data_namespace: "TriAevum",
    package_identity: "io.github.coccofresco.TriAevum",
    forge_product: "TriAevum Forge",
    // EUR (tools/triaevum_release/supported_revisions.json).
    code_sha256: &["16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220"],
    offline_interior_sun: true,
};

pub const MM3D: ForgeTitle = ForgeTitle {
    id: "mm3d",
    display_name: "The Legend of Zelda: Majora's Mask 3D",
    packaged_recipes: "recipes/mm3d.json",
    embedded_recipes: "titles/mm3d/forge/supported_revisions.json",
    data_namespace: "TriAevum-MM3D",
    package_identity: "io.github.coccofresco.TriAevum.MM3D",
    forge_product: "TriAevum Forge for Majora's Mask 3D",
    // EUR CTR-P-AJRP code.bin (== USA v1.1), titles/mm3d/forge recipe.
    code_sha256: &["6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d"],
    // MM3D has no interior-sun prebuild (its scene inventory is not OoT3D's;
    // the runtime reads no interior-sun cache for MM3D).
    offline_interior_sun: false,
};

/// Every title this source tree knows (for naming a foreign dump only).
pub const KNOWN: [ForgeTitle; 2] = [OOT3D, MM3D];

/// The title this Forge build prepares.
#[cfg(not(feature = "title-mm3d"))]
pub const ACTIVE: ForgeTitle = OOT3D;
#[cfg(feature = "title-mm3d")]
pub const ACTIVE: ForgeTitle = MM3D;

impl ForgeTitle {
    fn knows_code(&self, code_sha256: &str) -> bool {
        let digest = code_sha256.to_ascii_lowercase();
        self.code_sha256.iter().any(|known| *known == digest)
    }
}

/// The other known title whose code.bin this is, if any (never `active`).
pub fn foreign_title(active: &ForgeTitle, code_sha256: &str) -> Option<ForgeTitle> {
    KNOWN.into_iter().find(|title| title.id != active.id && title.knows_code(code_sha256))
}

/// The diagnostic for a dump of another title.
pub fn foreign_title_message(active: &ForgeTitle, foreign: &ForgeTitle) -> String {
    format!(
        "this is a dump of {} ({}); this {} prepares only {} ({}). Use the Forge for {}.",
        foreign.display_name, foreign.id, active.forge_product, active.display_name, active.id, foreign.display_name
    )
}

/// Whether a runtime whose product information lists `titles` (its
/// `titles` array: the adapters the build contains) can run `active`. A
/// runtime that predates the field carries only OoT3D.
pub fn runtime_title_error(active: &ForgeTitle, titles: Option<&serde_json::Value>) -> Option<String> {
    let carries = match titles {
        None => active.id == OOT3D.id,
        Some(serde_json::Value::Array(items)) => items.iter().any(|item| item.as_str() == Some(active.id)),
        Some(_) => return Some("Runtime product information lists its titles incorrectly".to_owned()),
    };
    (!carries).then(|| {
        format!(
            "This TriAevum runtime does not contain {} ({}); {} needs a runtime built with that title.",
            active.display_name, active.id, active.forge_product
        )
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn runtime_must_carry_the_forge_title() {
        use serde_json::json;
        let both = json!(["oot3d", "mm3d"]);
        let oot_only = json!(["oot3d"]);
        assert_eq!(runtime_title_error(&OOT3D, Some(&both)), None);
        assert_eq!(runtime_title_error(&MM3D, Some(&both)), None);
        assert_eq!(runtime_title_error(&OOT3D, Some(&oot_only)), None);
        // An older runtime (no field) is the OoT3D runtime, unchanged.
        assert_eq!(runtime_title_error(&OOT3D, None), None);
        // Negatives: the MM3D Forge refuses a runtime without MM3D, naming it.
        let refusal = runtime_title_error(&MM3D, Some(&oot_only)).unwrap();
        assert!(refusal.contains("Majora's Mask 3D") && refusal.contains("(mm3d)"), "{refusal}");
        assert!(runtime_title_error(&MM3D, None).is_some());
        assert!(runtime_title_error(&OOT3D, Some(&json!(["mm3d"]))).is_some());
        assert!(runtime_title_error(&OOT3D, Some(&json!("oot3d"))).is_some());
    }

    #[test]
    fn titles_are_isolated() {
        assert_ne!(OOT3D.packaged_recipes, MM3D.packaged_recipes);
        assert_ne!(OOT3D.embedded_recipes, MM3D.embedded_recipes);
        assert_ne!(OOT3D.data_namespace, MM3D.data_namespace);
        assert_ne!(OOT3D.package_identity, MM3D.package_identity);
        let mm = MM3D.code_sha256[0];
        let oot = OOT3D.code_sha256[0];
        assert_eq!(foreign_title(&OOT3D, mm), Some(MM3D));
        assert_eq!(foreign_title(&MM3D, &oot.to_ascii_uppercase()), Some(OOT3D));
        // Negative: the own title is never foreign; an unknown dump or a
        // digest prefix names no title.
        assert_eq!(foreign_title(&MM3D, mm), None);
        assert_eq!(foreign_title(&OOT3D, &"ab".repeat(32)), None);
        assert_eq!(foreign_title(&MM3D, &oot[..16]), None);
        let message = foreign_title_message(&OOT3D, &MM3D);
        assert!(message.contains("Majora's Mask 3D") && message.contains("(mm3d)"));
    }

    #[test]
    fn oot3d_forge_paths_are_unchanged() {
        assert_eq!(OOT3D.packaged_recipes, "recipes/oot3d.json");
        assert_eq!(OOT3D.embedded_recipes, "tools/triaevum_release/supported_revisions.json");
        assert_eq!(OOT3D.data_namespace, "TriAevum");
        assert_eq!(OOT3D.package_identity, "io.github.coccofresco.TriAevum");
    }

    #[test]
    fn active_title_matches_the_build() {
        #[cfg(feature = "title-mm3d")]
        assert_eq!(ACTIVE, MM3D);
        #[cfg(not(feature = "title-mm3d"))]
        assert_eq!(ACTIVE, OOT3D);
    }
}
