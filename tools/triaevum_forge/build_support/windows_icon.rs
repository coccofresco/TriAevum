// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Windows icon resource of a Forge executable, without a resource
// compiler or a build-dependency crate: the build script turns the checked-in
// `.ico` (tools/triaevum_release/branding/windows, made by
// generate_branding.py) into a compiled resource file in OUT_DIR and hands it
// to the linker, which accepts `.res` inputs (link.exe through CVTRES, and
// lld-link). Included by the build scripts with `include!`.

/// Embeds `ico` (relative to the package's manifest folder) as icon group 1 of
/// the package's binaries when building for Windows (MSVC); a no-op elsewhere.
fn embed_windows_icon(ico: &str) {
    let manifest = std::path::PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let ico = manifest.join(ico);
    println!("cargo:rerun-if-changed={}", ico.display());
    let windows = std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows");
    if !windows || std::env::var("CARGO_CFG_TARGET_ENV").as_deref() != Ok("msvc") {
        return;
    }
    let bytes = std::fs::read(&ico).unwrap_or_else(|error| panic!("cannot read {}: {error}", ico.display()));
    let resource = icon_resource(&bytes).unwrap_or_else(|error| panic!("{}: {error}", ico.display()));
    let output = std::path::PathBuf::from(std::env::var_os("OUT_DIR").expect("OUT_DIR")).join("icon.res");
    std::fs::write(&output, resource).unwrap_or_else(|error| panic!("cannot write {}: {error}", output.display()));
    println!("cargo:rustc-link-arg-bins={}", output.display());
}

const RT_ICON: u16 = 3;
const RT_GROUP_ICON: u16 = 14;
/// MOVEABLE | DISCARDABLE, and PURE for the group, as resource compilers write them.
const ICON_FLAGS: u16 = 0x1010;
const GROUP_FLAGS: u16 = 0x1030;

/// One RESOURCEHEADER with ordinal type and name (32 bytes), the data, and
/// the padding to the next DWORD. Language neutral.
fn push_resource(resource: &mut Vec<u8>, kind: u16, name: u16, flags: u16, data: &[u8]) {
    resource.extend_from_slice(&(data.len() as u32).to_le_bytes());
    resource.extend_from_slice(&32u32.to_le_bytes());
    for word in [0xFFFF, kind, 0xFFFF, name] {
        resource.extend_from_slice(&u16::to_le_bytes(word));
    }
    resource.extend_from_slice(&0u32.to_le_bytes()); // data version
    resource.extend_from_slice(&flags.to_le_bytes());
    resource.extend_from_slice(&0u16.to_le_bytes()); // language
    resource.extend_from_slice(&0u32.to_le_bytes()); // version
    resource.extend_from_slice(&0u32.to_le_bytes()); // characteristics
    resource.extend_from_slice(data);
    while resource.len() % 4 != 0 {
        resource.push(0);
    }
}

/// A `.res` file holding every image of an `.ico` file (RT_ICON 1..n) and
/// their directory (RT_GROUP_ICON 1).
fn icon_resource(ico: &[u8]) -> Result<Vec<u8>, String> {
    let word = |offset: usize| ico.get(offset..offset + 2).map(|b| u16::from_le_bytes([b[0], b[1]]));
    let dword = |offset: usize| ico.get(offset..offset + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]));
    if word(0) != Some(0) || word(2) != Some(1) {
        return Err("not an icon file".into());
    }
    let count = word(4).ok_or("truncated icon directory")?;
    if count == 0 {
        return Err("the icon file has no images".into());
    }
    let mut resource = Vec::new();
    push_resource(&mut resource, 0, 0, 0, &[]); // the empty header that starts every .res file
    let mut group = Vec::new();
    group.extend_from_slice(&[0, 0, 1, 0]);
    group.extend_from_slice(&count.to_le_bytes());
    for index in 0..count {
        let entry = 6 + 16 * index as usize;
        let header = ico.get(entry..entry + 12).ok_or("truncated icon directory")?;
        let size = dword(entry + 8).ok_or("truncated icon directory")? as usize;
        let offset = dword(entry + 12).ok_or("truncated icon directory")? as usize;
        let image = ico.get(offset..offset + size).ok_or("an icon image lies outside the file")?;
        let id = index + 1;
        push_resource(&mut resource, RT_ICON, id, ICON_FLAGS, image);
        group.extend_from_slice(header); // width, height, colours, reserved, planes, bit count, size
        group.extend_from_slice(&id.to_le_bytes());
    }
    push_resource(&mut resource, RT_GROUP_ICON, 1, GROUP_FLAGS, &group);
    Ok(resource)
}
