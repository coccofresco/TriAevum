// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Window geometry (issue #111): start around 720x560 logical points, fit
//! the monitor's work area including the title bar, keep a small fixed
//! minimum and never grow the minimum to the content, which scrolls instead.

#[cfg(any(windows, test))]
use eframe::egui::pos2;
use eframe::egui::{self, vec2, Rect, Vec2, ViewportCommand};

pub const INITIAL_SIZE: Vec2 = vec2(720.0, 560.0);
pub const MIN_SIZE: Vec2 = vec2(360.0, 300.0);
/// Without a work-area query, leave room for panels and the title bar as the
/// Python window did with the screen size.
const SCREEN_MARGIN: Vec2 = vec2(40.0, 96.0);

/// `WIDTHxHEIGHT` in logical points.
pub fn parse_work_area(text: &str) -> Option<Vec2> {
    let (width, height) = text.split_once(['x', 'X'])?;
    let (width, height): (f32, f32) = (width.trim().parse().ok()?, height.trim().parse().ok()?);
    (width > 0.0 && height > 0.0).then_some(vec2(width, height))
}

/// The inner size that fits `work_area` once `decoration` (title bar and
/// borders) is added; never below the minimum, never above the initial size.
pub fn fitted_inner_size(work_area: Vec2, decoration: Vec2) -> Vec2 {
    (work_area - decoration).min(INITIAL_SIZE).max(MIN_SIZE)
}

/// Where the window goes: centred in the work area, its top-left corner
/// inside it.
fn centred(area: Rect, outer: Vec2) -> egui::Pos2 {
    let offset = ((area.size() - outer) / 2.0).max(Vec2::ZERO);
    area.min + offset
}

#[derive(Default)]
pub struct Geometry {
    /// A simulated work area (`--work-area`), at the real work area's origin.
    pub override_area: Option<Vec2>,
    pub applied: bool,
    pub work_area: Option<Vec2>,
    pub decoration: Vec2,
}

impl Geometry {
    pub fn new(override_area: Option<Vec2>) -> Self {
        Self { override_area, ..Self::default() }
    }

    /// Fits the window once its native size and monitor are known.
    pub fn update(&mut self, context: &egui::Context, frame: &eframe::Frame) {
        if self.applied {
            return;
        }
        let info = context.input(|input| input.viewport().clone());
        let Some(inner) = info.inner_rect else { return };
        let platform = platform_work_area(frame, context.pixels_per_point());
        let area = match (self.override_area, platform) {
            (Some(size), platform) => Some((Some(platform.map_or(egui::Pos2::ZERO, |area| area.min)), size)),
            (None, Some(area)) => Some((Some(area.min), area.size())),
            (None, None) => info.monitor_size.map(|size| (None, size - SCREEN_MARGIN)),
        };
        let Some((origin, size)) = area else { return };
        let decoration = info.outer_rect.map_or(Vec2::ZERO, |outer| (outer.size() - inner.size()).max(Vec2::ZERO));
        let fitted = fitted_inner_size(size, decoration);
        if (fitted - inner.size()).abs().max_elem() > 0.5 {
            context.send_viewport_cmd(ViewportCommand::InnerSize(fitted));
        }
        if let Some(origin) = origin {
            let area = Rect::from_min_size(origin, size);
            let outer = info.outer_rect.map_or(inner, |outer| outer);
            if !area.contains_rect(Rect::from_min_size(outer.min, fitted + decoration)) {
                context.send_viewport_cmd(ViewportCommand::OuterPosition(centred(area, fitted + decoration)));
            }
        }
        self.work_area = Some(size);
        self.decoration = decoration;
        self.applied = true;
    }
}

/// The work area (desktop minus taskbars) of the window's monitor, in points.
#[cfg(windows)]
fn platform_work_area(frame: &eframe::Frame, pixels_per_point: f32) -> Option<Rect> {
    use raw_window_handle::{HasWindowHandle, RawWindowHandle};
    use windows_sys::Win32::Graphics::Gdi::{
        GetMonitorInfoW, MonitorFromWindow, MONITORINFO, MONITOR_DEFAULTTONEAREST,
    };

    let RawWindowHandle::Win32(handle) = frame.window_handle().ok()?.as_raw() else { return None };
    let window = handle.hwnd.get() as windows_sys::Win32::Foundation::HWND;
    // SAFETY: plain Win32 queries on our own live window handle.
    let work = unsafe {
        let monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        let mut info: MONITORINFO = std::mem::zeroed();
        info.cbSize = std::mem::size_of::<MONITORINFO>() as u32;
        if GetMonitorInfoW(monitor, &mut info) == 0 {
            return None;
        }
        info.rcWork
    };
    let point = |x: i32, y: i32| pos2(x as f32 / pixels_per_point, y as f32 / pixels_per_point);
    Some(Rect::from_min_max(point(work.left, work.top), point(work.right, work.bottom)))
}

#[cfg(not(windows))]
fn platform_work_area(_frame: &eframe::Frame, _pixels_per_point: f32) -> Option<Rect> {
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn work_areas_parse() {
        assert_eq!(parse_work_area("1320x672"), Some(vec2(1320.0, 672.0)));
        assert_eq!(parse_work_area("800X500"), Some(vec2(800.0, 500.0)));
        assert_eq!(parse_work_area("800"), None);
        assert_eq!(parse_work_area("0x500"), None);
    }

    #[test]
    fn the_window_fits_small_work_areas_including_its_title_bar() {
        let title_bar = vec2(16.0, 39.0);
        assert_eq!(fitted_inner_size(vec2(1320.0, 672.0), title_bar), INITIAL_SIZE);
        assert_eq!(fitted_inner_size(vec2(1100.0, 560.0), title_bar), vec2(720.0, 521.0));
        assert_eq!(fitted_inner_size(vec2(800.0, 500.0), title_bar), vec2(720.0, 461.0));
        assert_eq!(fitted_inner_size(vec2(300.0, 200.0), title_bar), MIN_SIZE);
    }

    #[test]
    fn the_window_is_centred_inside_the_work_area() {
        let area = Rect::from_min_size(pos2(0.0, 0.0), vec2(800.0, 500.0));
        assert_eq!(centred(area, vec2(736.0, 500.0)), pos2(32.0, 0.0));
        assert_eq!(centred(area, vec2(900.0, 600.0)), pos2(0.0, 0.0));
    }
}
