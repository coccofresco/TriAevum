// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! `update` (UX plan WP-04B): update this Windows installation from a
//! downloaded release ZIP or an extracted package folder.
//!
//! - `update ZIP` checks the package (publisher signature, inventory,
//!   catalogue), stages it and hands off to the update helper, then exits:
//!   the helper replaces the files once this process has ended, binds the
//!   prepared game and writes its result (`update --result` prints it).
//! - `update --update-target OLD` run from a newly extracted package updates
//!   the installation in OLD with this package (the way from beta.1.2,
//!   whose Python Forge has no Update button).
//! - `update ZIP --plan` previews without writing; `update --recover`
//!   finishes or undoes an interrupted update.

use std::path::PathBuf;
use std::process::ExitCode;

use clap::Args;
use forge_core::publish::HudLayout;
use forge_core::update::{UpdateRequest, UpdateSource};
use serde_json::{json, Value};

#[derive(Args)]
pub struct UpdateArgs {
    /// The downloaded TriAevum ZIP, or an extracted package folder (default with
    /// --update-target: this executable's package).
    pub source: Option<PathBuf>,
    /// The installation to update (default: this executable's package).
    #[arg(long, visible_alias = "update-target")]
    pub package_root: Option<PathBuf>,
    /// The private data folder (default: the installation's data folder).
    #[arg(long)]
    pub data_root: Option<PathBuf>,
    /// Reset the TopScreen HUD layout to the standard profile (default: keep yours).
    #[arg(long)]
    pub standard_hud_layout: bool,
    /// Install an older package over a newer one.
    #[arg(long)]
    pub allow_downgrade: bool,
    /// Accept a package without a trusted publisher signature. Needs also the
    /// environment variable TRIAEVUM_FORGE_ALLOW_UNSIGNED_UPDATE=unsigned-developer-package.
    #[arg(long)]
    pub allow_unsigned_developer_package: bool,
    /// Print what the update would change; write nothing.
    #[arg(long)]
    pub plan: bool,
    /// Open the updated Forge window when the update finishes.
    #[arg(long)]
    pub relaunch: bool,
    /// Finish or undo an interrupted update (nothing else).
    #[arg(long, conflicts_with_all = ["source", "plan", "result"])]
    pub recover: bool,
    /// Print the result of the last update.
    #[arg(long, conflicts_with_all = ["source", "plan"])]
    pub result: bool,
}

pub fn update(args: UpdateArgs, executable_package: impl Fn() -> Result<PathBuf, String>) -> Result<Value, String> {
    let targeted = args.package_root.is_some();
    let installation = match args.package_root.clone() {
        Some(root) => root,
        None => executable_package()?,
    };
    let activation = forge_core::install::package_layout(&installation).map_err(|error| error.message)?.activation;
    if args.recover {
        let recovered = forge_core::update_journal::recover(&activation).map_err(|error| error.message)?;
        forge_core::update::cleanup_stale(&activation);
        return Ok(json!({"recovered": recovered}));
    }
    if args.result {
        let path = forge_core::update_journal::result_path(&activation);
        return Ok(forge_core::update::read_result(&path).map(Value::Object).unwrap_or(json!({"result": null})));
    }
    let source = match (args.source, targeted) {
        (Some(source), _) => source,
        (None, true) => executable_package()?,
        (None, false) => {
            return Err("Choose the downloaded TriAevum ZIP (or use --update-target from a new package)".into())
        }
    };
    let source = if source.is_dir() { UpdateSource::Folder(source) } else { UpdateSource::Archive(source) };
    let request = UpdateRequest {
        data_root: args.data_root,
        hud_layout: if args.standard_hud_layout { HudLayout::Standard } else { HudLayout::Keep },
        allow_downgrade: args.allow_downgrade,
        allow_unsigned: args.allow_unsigned_developer_package,
        relaunch: args.relaunch,
        ..UpdateRequest::new(source, installation)
    };
    if args.plan {
        let options = forge_core::update::PlanOptions::publisher(&request);
        return match forge_core::update::plan(&request, &options) {
            Ok(forge_core::update::Planned::Done(answer)) => Ok(answer),
            Ok(forge_core::update::Planned::Ready(plan)) => Ok(plan.summary()),
            Err(error) => Err(super::explained(error)),
        };
    }
    let mut report = |stage: &str, message: &str| eprintln!("[{stage}] {message}");
    forge_core::update::update(&request, &mut report, &forge_core::CancelToken::new()).map_err(super::explained)
}

pub fn run(args: UpdateArgs, executable_package: impl Fn() -> Result<PathBuf, String>) -> ExitCode {
    match update(args, executable_package) {
        Ok(result) => super::print_result(&result),
        Err(message) => super::print_error(&message),
    }
}
