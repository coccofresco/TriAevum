// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The executable's Windows icon: the Forge icon of the project's branding
//! (tools/triaevum_release/branding/README.md).

include!("../../build_support/windows_icon.rs");

fn main() {
    embed_windows_icon("../../../triaevum_release/branding/windows/forge.ico");
}
