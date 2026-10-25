// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The installation this Forge belongs to (`bundle_paths.installation_root`,
//! `forge_gui.default_gui_data_root`, `forge_gui.runtime_path`): the package
//! is the executable's folder, like a frozen Python Forge.

use std::path::{Path, PathBuf};

use forge_base::{ForgeError, Result};
use forge_core::layout::{self, HostLayout, HostOs, ReleasePlatform};
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
        resolve(&layout::installed_runtime_path(&self.root, &host_platform()?))
    }
}

/// The release platform this build runs on (`release_platform.host_platform`).
pub fn host_platform() -> Result<ReleasePlatform> {
    layout::for_target(forge_core::jobs::target_triple())
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
        std::fs::write(installation.root.join("recipes").join("oot3d.json"), b"{}").unwrap();
        assert!(installation.recipes().ends_with("recipes/oot3d.json"));
        if cfg!(all(windows, target_arch = "x86_64")) {
            assert_eq!(installation.runtime_path().unwrap(), installation.root.join("TriAevum.exe"));
        }
    }
}
