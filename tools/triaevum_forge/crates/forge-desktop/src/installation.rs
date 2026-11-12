// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The installation this Forge belongs to (`bundle_paths.installation_root`,
//! `forge_gui.default_gui_data_root`, `forge_gui.runtime_path`): the package
//! is the executable's folder, like a frozen Python Forge.

use std::path::{Path, PathBuf};

use forge_base::{ForgeError, Result};
use forge_core::layout::{self, HostLayout, HostOs};
use forge_core::state::{inspect, InspectRequest, InstallationState};
use forge_prepare::paths::resolve;

#[derive(Clone, Debug)]
pub struct Installation {
    pub root: PathBuf,
    pub layout: HostLayout,
    /// The private generated data (`data/` of the activation root).
    pub data_root: PathBuf,
}

impl Installation {
    pub fn current() -> Result<Self> {
        let executable =
            std::env::current_exe().map_err(|error| ForgeError::io("Cannot locate the Forge executable", error))?;
        let executable = resolve(&executable)?;
        Self::at(executable.parent().unwrap_or(Path::new(".")))
    }

    pub fn at(root: &Path) -> Result<Self> {
        let layout = layout::for_package(root, &layout::process_env, HostOs::current())?;
        let data_root = resolve(&layout.activation.join("data"))?;
        Ok(Self { root: layout.package.clone(), layout, data_root })
    }

    pub fn active_title_state(&self) -> PathBuf {
        self.data_root.join("active-title.json")
    }

    /// `forge.DEFAULT_RECIPES`; a packaged Forge is its own distribution root.
    pub fn recipes(&self) -> PathBuf {
        forge_catalog::default_recipes(&self.root, &self.root)
    }

    /// `forge_gui.runtime_path()`.
    pub fn runtime_path(&self) -> Result<PathBuf> {
        resolve(&layout::launch_runtime_path(&self.root)?)
    }

    /// The installation state (`forge_core::state::inspect`). An interrupted
    /// restore is recovered first (UX plan WP-05B), so Play is offered only
    /// for one coherent version of the user's files; when recovery cannot
    /// run now (the game holds the session), inspection reports it.
    pub fn state(&self) -> InstallationState {
        if forge_core::restore::pending_restore(&self.data_root).is_some() {
            if let Err(error) = crate::pages::data::recover(self) {
                forge_core::log::ForgeLog::new(&self.data_root, "restore_recover").error(&error);
            }
        }
        inspect(&InspectRequest { data_root: Some(self.data_root.clone()), ..InspectRequest::package(&self.root) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn data_lives_in_the_activation_root() {
        let directory = tempfile::tempdir().unwrap();
        let installation = Installation::at(directory.path()).unwrap();
        assert_eq!(installation.data_root, installation.layout.activation.join("data"));
        assert_eq!(installation.active_title_state(), installation.data_root.join("active-title.json"));
        std::fs::create_dir(installation.root.join("recipes")).unwrap();
        // The packaged recipes of this Forge build's title (recipes/oot3d.json
        // for the OoT3D Forge, recipes/mm3d.json for the MM3D Forge).
        let packaged = forge_catalog::recipes::PACKAGED_RECIPES;
        std::fs::write(installation.root.join(packaged), b"{}").unwrap();
        assert!(installation.recipes().ends_with(packaged));
        if cfg!(all(windows, target_arch = "x86_64")) {
            assert_eq!(installation.runtime_path().unwrap(), installation.root.join(forge_catalog::platform::WINDOWS_PRODUCT_EXECUTABLE));
        }
    }
}
