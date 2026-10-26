// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! The window's pages and the navigation between them.

pub mod about;
pub mod data;
pub mod diagnostics;
pub mod enhancements;
pub mod home;

use eframe::egui::{self, Button, RichText};

use crate::app::{Action, Recorder};
use crate::texts::{Msg, Texts};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Page {
    Home,
    Enhancements,
    Data,
    Diagnostics,
    About,
}

impl Page {
    pub const ALL: [Page; 5] = [Self::Home, Self::Enhancements, Self::Data, Self::Diagnostics, Self::About];

    pub fn code(self) -> &'static str {
        match self {
            Self::Home => "home",
            Self::Enhancements => "enhancements",
            Self::Data => "data",
            Self::Diagnostics => "diagnostics",
            Self::About => "about",
        }
    }

    fn label(self) -> Msg<'static> {
        match self {
            Self::Home => Msg::NavHome,
            Self::Enhancements => Msg::NavEnhancements,
            Self::Data => Msg::NavData,
            Self::Diagnostics => Msg::NavDiagnostics,
            Self::About => Msg::NavAbout,
        }
    }

    fn widget(self) -> &'static str {
        match self {
            Self::Home => "nav_home",
            Self::Enhancements => "nav_enhancements",
            Self::Data => "nav_data",
            Self::Diagnostics => "nav_diagnostics",
            Self::About => "nav_about",
        }
    }
}

/// The tabs; they wrap onto a second row on a narrow window.
pub fn navigation(ui: &mut egui::Ui, texts: &Texts, current: Page, record: &mut Recorder, actions: &mut Vec<Action>) {
    ui.horizontal_wrapped(|ui| {
        ui.spacing_mut().item_spacing.x = 4.0;
        for page in Page::ALL {
            let mut text = RichText::new(texts.get(page.label()));
            if page == current {
                text = text.strong();
            }
            let button = Button::new(text).selected(page == current).min_size(egui::vec2(0.0, 28.0));
            if record.button(ui, true, page.widget(), button) {
                actions.push(Action::Navigate(page));
            }
        }
    });
}
