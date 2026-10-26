// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The window's look: the Forge background art behind every page, a dark warm
//! theme with gold accents, and the semi-opaque panels that keep text readable
//! over the art (contrast notes in tools/triaevum_release/branding/README.md).
//! The art and the window icon are compiled in: nothing is looked up at run
//! time, so a missing file can never change what the window shows.

use eframe::egui::{self, pos2, vec2, Color32, CornerRadius, Frame, Margin, Rect, Stroke, Vec2};

/// The owner's artwork (tools/triaevum_release/branding).
const BACKGROUND_JPEG: &[u8] = include_bytes!("../../../../triaevum_release/branding/masters/forge_background.jpg");
const WINDOW_ICON_PNG: &[u8] = include_bytes!("../../../../triaevum_release/branding/forge/forge_window_icon.png");

/// The Master Sword in the art, as fractions of its width and height: the
/// crop keeps it in view.
const FOCUS: Vec2 = vec2(0.775, 0.45);
/// Where the sword lands across a window narrower than the art: near the
/// right edge, beside the page panel once the window is wide.
const FOCUS_TARGET_X: f32 = 0.80;
/// Below this width the page panel takes the whole width; above it the panel
/// keeps 62 % and the art shows on the right.
const WIDE_PANEL: f32 = 760.0;

pub const BASE: Color32 = Color32::from_rgb(14, 11, 9);
pub const TEXT: Color32 = Color32::from_rgb(238, 228, 212);
pub const TEXT_STRONG: Color32 = Color32::from_rgb(255, 248, 236);
pub const TEXT_WEAK: Color32 = Color32::from_rgb(200, 186, 166);
/// The primary action and the current tab: gold with dark text.
pub const GOLD: Color32 = Color32::from_rgb(224, 178, 84);
pub const ON_GOLD: Color32 = Color32::from_rgb(30, 20, 8);
/// Accent text and outlines on the dark panels.
pub const GOLD_LIGHT: Color32 = Color32::from_rgb(242, 204, 116);
const GOLD_DIM: Color32 = Color32::from_rgb(160, 122, 58);
/// Selected choices and text selection: light text stays readable on it.
const BRONZE: Color32 = Color32::from_rgb(104, 72, 22);
pub const WARN: Color32 = Color32::from_rgb(255, 172, 88);
pub const ERROR: Color32 = Color32::from_rgb(255, 128, 114);

/// The page panel, the tab strip and the footer over the art. The page panel's
/// opacity is the readability floor: body and weak text keep WCAG AA (4.5:1)
/// over the art's brightest light shaft.
pub const PANEL: Color32 = Color32::from_rgba_unmultiplied_const(18, 14, 11, 226);
pub const STRIP: Color32 = Color32::from_rgba_unmultiplied_const(12, 9, 7, 214);
pub const FOOTER: Color32 = Color32::from_rgba_unmultiplied_const(13, 10, 8, 246);
const PANEL_EDGE: Color32 = Color32::from_rgba_unmultiplied_const(214, 168, 84, 64);

/// The dark theme. Every colour a page reads from the visuals (weak text,
/// warnings, errors, the accent `selection.stroke`) is chosen for the panels.
pub fn visuals() -> egui::Visuals {
    let mut visuals = egui::Visuals::dark();
    visuals.panel_fill = BASE;
    visuals.window_fill = Color32::from_rgb(24, 19, 15);
    visuals.window_stroke = Stroke::new(1.0, Color32::from_rgb(112, 86, 46));
    visuals.window_corner_radius = CornerRadius::same(10);
    visuals.menu_corner_radius = CornerRadius::same(6);
    visuals.extreme_bg_color = Color32::from_rgb(10, 8, 6);
    visuals.faint_bg_color = Color32::from_rgb(32, 26, 20);
    visuals.code_bg_color = Color32::from_rgb(32, 26, 20);
    visuals.hyperlink_color = GOLD_LIGHT;
    visuals.warn_fg_color = WARN;
    visuals.error_fg_color = ERROR;
    visuals.weak_text_color = Some(TEXT_WEAK);
    visuals.selection.bg_fill = BRONZE;
    visuals.selection.stroke = Stroke::new(1.0, GOLD_LIGHT);
    visuals.text_cursor.stroke = Stroke::new(2.0, GOLD_LIGHT);
    let widgets = &mut visuals.widgets;
    let fill = |widget: &mut egui::style::WidgetVisuals, color: Color32| {
        widget.bg_fill = color;
        widget.weak_bg_fill = color;
        widget.corner_radius = CornerRadius::same(6);
    };
    fill(&mut widgets.noninteractive, Color32::from_rgb(26, 21, 17));
    widgets.noninteractive.bg_stroke = Stroke::new(1.0, Color32::from_rgb(82, 64, 42));
    widgets.noninteractive.fg_stroke = Stroke::new(1.0, TEXT);
    fill(&mut widgets.inactive, Color32::from_rgb(46, 37, 28));
    widgets.inactive.bg_stroke = Stroke::new(1.0, Color32::from_rgb(96, 75, 48));
    widgets.inactive.fg_stroke = Stroke::new(1.0, TEXT);
    fill(&mut widgets.hovered, Color32::from_rgb(64, 50, 36));
    widgets.hovered.bg_stroke = Stroke::new(1.0, GOLD_DIM);
    widgets.hovered.fg_stroke = Stroke::new(1.5, TEXT_STRONG);
    fill(&mut widgets.active, Color32::from_rgb(82, 64, 44));
    widgets.active.bg_stroke = Stroke::new(1.0, GOLD);
    widgets.active.fg_stroke = Stroke::new(2.0, TEXT_STRONG);
    fill(&mut widgets.open, Color32::from_rgb(46, 37, 28));
    widgets.open.bg_stroke = Stroke::new(1.0, GOLD_DIM);
    widgets.open.fg_stroke = Stroke::new(1.0, TEXT_STRONG);
    visuals
}

/// The page panel: content scrolls inside it.
pub fn page_frame() -> Frame {
    Frame::new()
        .fill(PANEL)
        .stroke(Stroke::new(1.0, PANEL_EDGE))
        .corner_radius(10)
        .inner_margin(Margin::symmetric(12, 10))
}

/// The tab strip along the top edge.
pub fn strip_frame(margin: Margin) -> Frame {
    Frame::new().fill(STRIP).inner_margin(margin)
}

/// The footer with the status and the primary action.
pub fn footer_frame(margin: Margin) -> Frame {
    Frame::new().fill(FOOTER).inner_margin(margin)
}

/// The line between the page and the footer, and under the tab strip.
pub fn edge_stroke() -> Stroke {
    Stroke::new(1.0, PANEL_EDGE)
}

/// The page panel's outer width for `available` points: all of it on a small
/// window, 62 % (at least `WIDE_PANEL`) on a wide one.
pub fn panel_width(available: f32) -> f32 {
    if available <= WIDE_PANEL {
        available
    } else {
        (available * 0.62).max(WIDE_PANEL)
    }
}

/// The part of the art (UV coordinates) that covers `window`: scaled to fill
/// it without stretching, cropped around the sword.
pub fn cover_uv(image: Vec2, window: Vec2) -> Rect {
    let scale = (window.x / image.x).max(window.y / image.y);
    let visible = (window / scale).min(image);
    let left = (FOCUS.x * image.x - FOCUS_TARGET_X * visible.x).clamp(0.0, image.x - visible.x);
    let top = (FOCUS.y * image.y - 0.5 * visible.y).clamp(0.0, image.y - visible.y);
    Rect::from_min_size(pos2(left / image.x, top / image.y), visible / image)
}

/// The window icon (the Forge icon); the platform's default icon if the
/// compiled-in image could not be decoded.
pub fn window_icon() -> egui::IconData {
    eframe::icon_data::from_png_bytes(WINDOW_ICON_PNG).unwrap_or_default()
}

/// The background art, uploaded once.
pub struct Backdrop {
    texture: Option<egui::TextureHandle>,
}

impl Backdrop {
    /// Installs the theme and uploads the art.
    pub fn install(context: &egui::Context) -> Self {
        context.set_theme(egui::ThemePreference::Dark);
        context.set_visuals_of(egui::Theme::Dark, visuals());
        let texture = image::load_from_memory_with_format(BACKGROUND_JPEG, image::ImageFormat::Jpeg).ok().map(|art| {
            let art = art.into_rgb8();
            let size = [art.width() as usize, art.height() as usize];
            let image = egui::ColorImage::from_rgb(size, art.as_raw());
            let options =
                egui::TextureOptions { mipmap_mode: Some(egui::TextureFilter::Linear), ..egui::TextureOptions::LINEAR };
            context.load_texture("forge-background", image, options)
        });
        Self { texture }
    }

    /// Paints the art over `rect`, or the plain base colour without it.
    pub fn paint(&self, painter: &egui::Painter, rect: Rect) {
        painter.rect_filled(rect, 0.0, BASE);
        if let Some(texture) = &self.texture {
            painter.image(texture.id(), rect, cover_uv(texture.size_vec2(), rect.size()), Color32::WHITE);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const ART: Vec2 = vec2(1672.0, 941.0);

    fn sword_x(window: Vec2) -> f32 {
        let uv = cover_uv(ART, window);
        (FOCUS.x - uv.min.x) / uv.width() * window.x
    }

    #[test]
    fn the_art_covers_without_stretching() {
        for window in [vec2(720.0, 560.0), vec2(400.0, 560.0), vec2(1920.0, 1000.0), vec2(2560.0, 600.0)] {
            let uv = cover_uv(ART, window);
            assert!(uv.min.x >= 0.0 && uv.min.y >= 0.0 && uv.max.x <= 1.0001 && uv.max.y <= 1.0001, "{window:?}");
            let aspect = uv.width() * ART.x / (uv.height() * ART.y);
            assert!((aspect - window.x / window.y).abs() < 0.01, "{window:?}");
        }
    }

    #[test]
    fn the_sword_stays_in_view_beside_a_wide_page() {
        // A wide window: the sword is right of the page panel.
        for window in [vec2(1280.0, 700.0), vec2(1920.0, 1000.0), vec2(1100.0, 620.0)] {
            let panel = panel_width(window.x - 16.0) + 8.0;
            let x = sword_x(window);
            assert!(x > panel && x < window.x, "{window:?}: sword at {x}, panel to {panel}");
        }
        // A narrow one still shows it, near the right edge.
        for window in [vec2(720.0, 560.0), vec2(400.0, 560.0)] {
            let x = sword_x(window);
            assert!(x > window.x * 0.5 && x < window.x, "{window:?}: sword at {x}");
        }
    }

    #[test]
    fn the_art_is_compiled_in() {
        let art = image::load_from_memory_with_format(BACKGROUND_JPEG, image::ImageFormat::Jpeg).unwrap();
        assert_eq!((art.width(), art.height()), (1672, 941));
        let icon = window_icon();
        assert_eq!((icon.width, icon.height), (256, 256));
    }

    fn linear(channel: u8) -> f64 {
        let c = channel as f64 / 255.0;
        if c <= 0.04045 {
            c / 12.92
        } else {
            ((c + 0.055) / 1.055).powf(2.4)
        }
    }

    fn luminance(color: [u8; 3]) -> f64 {
        0.2126 * linear(color[0]) + 0.7152 * linear(color[1]) + 0.0722 * linear(color[2])
    }

    fn contrast(a: [u8; 3], b: [u8; 3]) -> f64 {
        let (a, b) = (luminance(a), luminance(b));
        (a.max(b) + 0.05) / (a.min(b) + 0.05)
    }

    fn rgb(color: Color32) -> [u8; 3] {
        [color.r(), color.g(), color.b()]
    }

    /// `over` (unmultiplied, with alpha) blended onto `under` in gamma space,
    /// as egui's painter blends.
    fn blend(over: Color32, under: [u8; 3]) -> [u8; 3] {
        let [r, g, b, a] = over.to_srgba_unmultiplied();
        let alpha = a as f64 / 255.0;
        let mix = |top: u8, bottom: u8| (top as f64 * alpha + bottom as f64 * (1.0 - alpha)).round() as u8;
        [mix(r, under[0]), mix(g, under[1]), mix(b, under[2])]
    }

    #[test]
    fn text_on_the_page_panel_meets_wcag_aa_over_the_brightest_art() {
        // The brightest pixel anywhere in the art (the sun in the window),
        // whatever part of it a window size puts under the panel.
        let art = image::load_from_memory_with_format(BACKGROUND_JPEG, image::ImageFormat::Jpeg).unwrap().into_rgb8();
        let brightest =
            art.pixels().map(|pixel| pixel.0).max_by(|a, b| luminance(*a).total_cmp(&luminance(*b))).unwrap();
        let panel = blend(PANEL, brightest);
        // Cards on the panel add a faint tint of their accent (pages/home.rs).
        let card = blend(GOLD_LIGHT.gamma_multiply(0.08), panel);
        let error_card = blend(ERROR.gamma_multiply(0.08), panel);
        let texts =
            [("text", TEXT), ("weak text", TEXT_WEAK), ("accent", GOLD_LIGHT), ("warning", WARN), ("error", ERROR)];
        for (name, color) in texts {
            for (background, under) in [("panel", panel), ("card", card), ("error card", error_card)] {
                let ratio = contrast(rgb(color), under);
                println!("{name} on the {background} over {brightest:?}: {ratio:.2}:1");
                assert!(ratio >= 4.5, "{name} on the {background}: {ratio:.2}:1");
            }
        }
        // Opaque widgets.
        let widgets = [
            ("dark text on gold", ON_GOLD, rgb(GOLD)),
            ("accent on a selected choice", GOLD_LIGHT, rgb(BRONZE)),
            ("text on a button", TEXT, rgb(visuals().widgets.inactive.weak_bg_fill)),
            ("text on the footer", TEXT, blend(FOOTER, brightest)),
            ("text on the tab strip", TEXT, blend(STRIP, brightest)),
        ];
        for (name, color, under) in widgets {
            let ratio = contrast(rgb(color), under);
            println!("{name}: {ratio:.2}:1");
            assert!(ratio >= 4.5, "{name}: {ratio:.2}:1");
        }
    }

    #[test]
    fn the_page_panel_leaves_room_on_wide_windows() {
        assert_eq!(panel_width(400.0), 400.0);
        assert_eq!(panel_width(760.0), 760.0);
        assert_eq!(panel_width(1000.0), 760.0);
        assert!((panel_width(1600.0) - 992.0).abs() < 0.01);
    }
}
