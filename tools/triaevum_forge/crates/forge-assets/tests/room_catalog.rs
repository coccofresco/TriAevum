use forge_assets::scene_lights::prepare_scene_lights;
use forge_base::{hashing::sha256_hex, CancelToken};
use serde_json::{json, Value};
use std::path::Path;

fn fixture(root: &Path) -> Value {
    std::fs::create_dir_all(root.join("rooms")).unwrap();
    let bytes = b"synthetic complete room bytes";
    std::fs::write(root.join("rooms/sample_0_info.zsi"), bytes).unwrap();
    let mut value = json!({"inputs": {"romfs": {"sha256": "a".repeat(64)}}, "room_catalog": {
        "version": 1, "title": "test-title", "directory": "rooms", "generator_sha256": "b".repeat(64),
        "filename_suffix": "_info.zsi", "index_separator": "_",
        "catalog": {"schema": "triaevum.scene-lights", "version": 1, "title": "test-title", "rejected": [], "rooms": [
            {"scene": 2, "room": 0, "source": "rooms/sample_0_info.zsi", "sourceSha256": sha256_hex(bytes),
             "fingerprint": "1234567890abcdef", "emitters": [{"category": "lamp", "position": [0, 1, 2],
                "colour": [0.5, 0.4, 0.1], "radius": 10, "gain": 1, "surfaces": ["1234567890abcdef"], "independentEmission": true}]}]}}});
    let catalog = value["room_catalog"].as_object_mut().unwrap().remove("catalog").unwrap();
    value["room_catalog"]["catalog_json"] = json!(catalog.to_string());
    value
}

#[test]
fn install_entrypoint_reuses_repairs_and_rejects_changed_or_unknown_rooms() {
    let tmp = tempfile::tempdir().unwrap();
    let romfs = tmp.path().join("romfs");
    let data = tmp.path().join("data");
    let recipe = fixture(&romfs);
    let cancel = CancelToken::new();
    let prepare = |r: &Value| prepare_scene_lights(&data, r, &romfs, None, &mut |_, _| {}, &cancel);
    let output = prepare(&recipe).unwrap().unwrap();
    let expected = std::fs::read(&output).unwrap();
    assert_eq!(prepare(&recipe).unwrap().unwrap(), output);
    std::fs::write(&output, b"broken").unwrap();
    prepare(&recipe).unwrap();
    assert_eq!(std::fs::read(&output).unwrap(), expected);
    std::fs::write(output.parent().unwrap().join("receipt.json"), b"{").unwrap();
    prepare(&recipe).unwrap();
    assert_eq!(std::fs::read(&output).unwrap(), expected);
    let mut changed = recipe.clone();
    changed["room_catalog"]["generator_sha256"] = json!("c".repeat(64));
    assert_ne!(prepare(&changed).unwrap().unwrap(), output);
    std::fs::write(romfs.join("rooms/uncovered_1_info.zsi"), b"unknown").unwrap();
    assert!(prepare(&recipe).is_err());
    std::fs::remove_file(romfs.join("rooms/uncovered_1_info.zsi")).unwrap();
    std::fs::write(romfs.join("rooms/sample_0_info.zsi"), b"changed").unwrap();
    assert!(prepare(&recipe).is_err(), "a valid receipt must not hide changed content");
    std::fs::remove_file(romfs.join("rooms/sample_0_info.zsi")).unwrap();
    assert!(prepare(&recipe).is_err());
}

#[test]
fn exclusions_are_hash_bound_and_still_require_complete_coverage() {
    let temp = tempfile::tempdir().unwrap();
    let mut r = fixture(temp.path());
    let cancel = CancelToken::new();
    let run = |r: &Value| prepare_scene_lights(temp.path(), r, temp.path(), None, &mut |_, _| {}, &cancel);
    std::fs::write(temp.path().join("rooms/unused_0_info.zsi"), b"unused").unwrap();
    r["room_catalog"]["excluded_rooms"] = json!([{"source": "rooms/unused_0_info.zsi", "sourceSha256": sha256_hex(b"unused"), "reason": "not-referenced-by-native-scene-table"}]);
    run(&r).unwrap();
    std::fs::write(temp.path().join("rooms/unused_0_info.zsi"), b"modified").unwrap();
    assert!(run(&r).is_err());
    std::fs::remove_file(temp.path().join("rooms/unused_0_info.zsi")).unwrap();
    assert!(run(&r).is_err());
}

#[test]
fn filename_convention_is_recipe_owned() {
    let tmp = tempfile::tempdir().unwrap();
    let mut recipe = fixture(tmp.path());
    std::fs::rename(tmp.path().join("rooms/sample_0_info.zsi"), tmp.path().join("rooms/sample-room0.bin")).unwrap();
    recipe["room_catalog"]["filename_suffix"] = json!(".bin");
    recipe["room_catalog"]["index_separator"] = json!("-room");
    let mut catalog: Value = serde_json::from_str(recipe["room_catalog"]["catalog_json"].as_str().unwrap()).unwrap();
    catalog["rooms"][0]["source"] = json!("rooms/sample-room0.bin");
    recipe["room_catalog"]["catalog_json"] = json!(catalog.to_string());
    prepare_scene_lights(tmp.path(), &recipe, tmp.path(), None, &mut |_, _| {}, &CancelToken::new()).unwrap();
}

#[test]
fn invalid_table_is_never_an_empty_success() {
    let tmp = tempfile::tempdir().unwrap();
    let recipe = fixture(tmp.path());
    for pointer in ["/room_catalog/title", "/room_catalog/version", "/room_catalog/generator_sha256",
        "/room_catalog/filename_suffix", "/room_catalog/index_separator",
        "/room_catalog/catalog/rooms/0/fingerprint", "/room_catalog/catalog/rooms/0/scene",
        "/room_catalog/catalog/rooms/0/emitters/0/independentEmission",
        "/room_catalog/catalog/rooms/0/emitters/0/radius",
        "/room_catalog/catalog/rooms/0/emitters/0/category"] {
        let mut broken = recipe.clone();
        if let Some(p) = pointer.strip_prefix("/room_catalog/catalog") {
            let mut catalog: Value = serde_json::from_str(broken["room_catalog"]["catalog_json"].as_str().unwrap()).unwrap();
            *catalog.pointer_mut(p).unwrap() = Value::Null;
            broken["room_catalog"]["catalog_json"] = json!(catalog.to_string());
        } else {
            *broken.pointer_mut(pointer).unwrap() = Value::Null;
        }
        assert!(prepare_scene_lights(tmp.path(), &broken, tmp.path(), None, &mut |_, _| {}, &CancelToken::new()).is_err(), "{pointer}");
    }
}

#[test]
#[ignore = "requires owner's local recipe, RomFS and Python oracle"]
fn real_publisher_table_matches_python_oracle() {
    let recipe: Value = serde_json::from_slice(&std::fs::read(std::env::var("TRIAEVUM_ROOM_RECIPE").unwrap()).unwrap()).unwrap();
    let tmp = tempfile::tempdir().unwrap();
    let romfs = std::env::var("TRIAEVUM_ROMFS").unwrap();
    let output = prepare_scene_lights(tmp.path(), &recipe, Path::new(&romfs), None, &mut |_, _| {}, &CancelToken::new()).unwrap().unwrap();
    let oracle = std::path::PathBuf::from(std::env::var("TRIAEVUM_ROOM_ORACLE").unwrap());
    assert_eq!(output.parent().unwrap().file_name(), oracle.parent().unwrap().file_name());
    let receipt = |p: &Path| -> Value { serde_json::from_slice(&std::fs::read(p.parent().unwrap().join("receipt.json")).unwrap()).unwrap() };
    assert_eq!(receipt(&output), receipt(&oracle));
    let actual = std::fs::read(output).unwrap();
    let expected = std::fs::read(oracle).unwrap();
    if let Ok(path) = std::env::var("TRIAEVUM_ROOM_RESULT") { std::fs::write(path, &actual).unwrap(); }
    assert!(actual == expected, "room oracle differs: first byte {:?}, lengths {} / {}",
        actual.iter().zip(&expected).position(|(a, b)| a != b), actual.len(), expected.len());
}
