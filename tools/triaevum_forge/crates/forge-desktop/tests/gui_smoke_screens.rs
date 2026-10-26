// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The real window laid out by `--gui-smoke` on small and scaled screens: a
//! card for every installation state and every page must fit the work
//! area, keep the primary action fully visible, clip nothing horizontally,
//! let every widget scroll into view and reach the pointer, and pass the
//! keyboard checks (UX plan WP-02; issue #111). Opening windows needs a
//! desktop session, so this runs only with `TRIAEVUM_FORGE_GUI_SMOKE=1`.

use std::process::Command;

/// Logical work area, display scale, Forge language, and what it stands for.
const SCREENS: &[(&str, &str, &str, &str)] = &[
    // The WP-02 qualification matrix (logical work areas).
    ("1360x768", "1", "en", "1360x768 at 100 %"),
    ("1360x768", "1.25", "en", "1360x768 at 125 %"),
    ("1360x768", "1.5", "en", "1360x768 at 150 %"),
    ("1360x768", "2", "en", "1360x768 at 200 %"),
    ("1280x800", "1", "en", "1280x800 at 100 %"),
    ("1280x800", "1.25", "en", "1280x800 at 125 %"),
    ("1280x800", "1.5", "en", "1280x800 at 150 %"),
    ("1280x800", "2", "en", "1280x800 at 200 %"),
    ("800x600", "1", "en", "800x600 at 100 %"),
    ("800x600", "1.25", "en", "800x600 at 125 %"),
    ("800x600", "1.5", "en", "800x600 at 150 %"),
    ("800x600", "2", "en", "800x600 at 200 %"),
    // Issue #111's reports and the smallest common laptops.
    ("1360x728", "1", "en", "1360x768 with a 40 px panel (Wayfire report)"),
    ("1280x688", "1.5", "en", "1920x1080 at 150 % with a 48 px taskbar (Windows report)"),
    ("910x485", "1.5", "en", "1366x768 at 150 % with a 40 px taskbar"),
    ("819x448", "1.25", "en", "1024x600 at 125 % with a 40 px taskbar"),
    // Narrower than the footer's stacking width.
    ("400x560", "1", "en", "a window narrowed to 400 points"),
    // Longer texts than any translation (UX plan WP-10).
    ("1360x768", "1.5", "pseudo", "the pseudo-locale at 1360x768 and 150 %"),
];

#[test]
fn every_state_fits_small_and_scaled_screens() {
    if std::env::var_os("TRIAEVUM_FORGE_GUI_SMOKE").is_none() {
        eprintln!("skipped: set TRIAEVUM_FORGE_GUI_SMOKE=1 in a desktop session");
        return;
    }
    let directory = tempfile::tempdir().unwrap();
    let output = directory.path().join("probe.json");
    for (area, scale, language, screen) in SCREENS {
        let _ = std::fs::remove_file(&output);
        let status = Command::new(env!("CARGO_BIN_EXE_TriAevumForge"))
            .arg("--gui-smoke")
            .arg("--output")
            .arg(&output)
            .args(["--work-area", area, "--pixels-per-point", scale, "--language", language, "--timeout", "180"])
            .status()
            .unwrap();
        let report: serde_json::Value = serde_json::from_slice(&std::fs::read(&output).unwrap()).unwrap();
        for key in ["fits_work_area", "primary_visible", "prepare_button_tracks_input"] {
            assert_eq!(report[key], true, "{screen}: {key}\n{report:#}");
        }
        assert_eq!(report["clipped_widgets"], serde_json::json!([]), "{screen}");
        assert_eq!(report["unmapped_widgets"], serde_json::json!([]), "{screen}");
        assert_eq!(report["scroll_check"]["failures"], serde_json::json!([]), "{screen}");
        assert!(report["scroll_check"]["checked"].as_u64() > Some(100), "{screen}");
        for (key, value) in report["keyboard"].as_object().unwrap() {
            if value.is_boolean() {
                assert_eq!(*value, true, "{screen}: keyboard {key}");
            }
        }
        assert_eq!(report["screens"].as_object().unwrap().len(), 17, "{screen}");
        assert_eq!(report["pixels_per_point"].as_f64(), scale.parse::<f64>().ok(), "{screen}");
        assert!(status.success() && report["status"] == "passed", "{screen}\n{report:#}");
    }
}
