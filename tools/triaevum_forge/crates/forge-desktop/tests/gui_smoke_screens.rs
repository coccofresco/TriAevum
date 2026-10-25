// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The real window laid out by `--gui-smoke` on the small and scaled screens
//! of issue #111: it must fit the work area, keep the footer with the
//! primary actions fully visible and clip nothing horizontally. Opening
//! windows needs a desktop session, so this runs only with
//! `TRIAEVUM_FORGE_GUI_SMOKE=1`.

use std::process::Command;

/// Logical work area, display scale, and the screen it stands for.
const SCREENS: &[(&str, &str, &str)] = &[
    ("1360x728", "1", "1360x768 with a 40 px panel (Wayfire report)"),
    ("1366x705", "1", "1366x768 with a 63 px taskbar"),
    ("1280x680", "1", "1280x720"),
    ("1024x600", "1", "1024x600 netbook, no panel"),
    ("1536x825", "1.25", "1920x1080 at 125 % with a 48 px taskbar"),
    ("1280x688", "1.5", "1920x1080 at 150 % with a 48 px taskbar (Windows report)"),
    ("1092x582", "1.25", "1366x768 at 125 % with a 40 px taskbar"),
    ("910x485", "1.5", "1366x768 at 150 % with a 40 px taskbar"),
    ("819x448", "1.25", "1024x600 at 125 % with a 40 px taskbar"),
];

#[test]
fn the_window_fits_small_and_scaled_screens() {
    if std::env::var_os("TRIAEVUM_FORGE_GUI_SMOKE").is_none() {
        eprintln!("skipped: set TRIAEVUM_FORGE_GUI_SMOKE=1 in a desktop session");
        return;
    }
    let directory = tempfile::tempdir().unwrap();
    let output = directory.path().join("probe.json");
    for (area, scale, screen) in SCREENS {
        let _ = std::fs::remove_file(&output);
        let status = Command::new(env!("CARGO_BIN_EXE_TriAevumForge"))
            .arg("--gui-smoke")
            .arg("--output")
            .arg(&output)
            .args(["--work-area", area, "--pixels-per-point", scale, "--timeout", "60"])
            .status()
            .unwrap();
        let report: serde_json::Value = serde_json::from_slice(&std::fs::read(&output).unwrap()).unwrap();
        for key in ["fits_work_area", "footer_visible", "prepare_button_tracks_input"] {
            assert_eq!(report[key], true, "{screen}: {key}\n{report:#}");
        }
        assert_eq!(report["clipped_widgets"], serde_json::json!([]), "{screen}");
        assert_eq!(report["pixels_per_point"].as_f64(), scale.parse::<f64>().ok(), "{screen}");
        assert!(status.success() && report["status"] == "passed", "{screen}\n{report:#}");
    }
}
