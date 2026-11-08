// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The executable's Windows icon: the Forge icon of the project's branding
//! (tools/triaevum_release/branding/README.md), or of the MM3D Forge's own
//! branding with the `title-mm3d` feature (titles/mm3d/branding/README.md).

include!("../../build_support/windows_icon.rs");

fn main() {
    println!("cargo:rerun-if-env-changed=CARGO_FEATURE_TITLE_MM3D");
    if std::env::var_os("CARGO_FEATURE_TITLE_MM3D").is_some() {
        embed_windows_icon("../../../../titles/mm3d/branding/windows/forge.ico");
    } else {
        embed_windows_icon("../../../triaevum_release/branding/windows/forge.ico");
    }
}
