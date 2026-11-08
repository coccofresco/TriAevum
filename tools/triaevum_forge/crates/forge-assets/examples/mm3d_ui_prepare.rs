// SPDX-License-Identifier: GPL-3.0-or-later
// Offline local-input acceptance driver for the same native Forge owner.
use forge_base::{
    CancelToken,
    hashing::{hex, sha256_file},
};
use std::path::Path;
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().collect();
    if args.len() != 5 {
        return Err("usage: mm3d_ui_prepare <package-root> <data-root> <romfs.bin> <code.bin>".into());
    }
    let cancel = CancelToken::new();
    let code = hex(&sha256_file(Path::new(&args[4]), &cancel)?.1);
    let romfs = hex(&sha256_file(Path::new(&args[3]), &cancel)?.1);
    let result = forge_assets::mm3d_ui::prepare(
        Path::new(&args[1]),
        Path::new(&args[2]),
        Path::new(&args[3]),
        &code,
        &romfs,
        &mut |stage, message| println!("{stage}: {message}"),
        &cancel,
    )?;
    println!("{result:?}");
    Ok(())
}
