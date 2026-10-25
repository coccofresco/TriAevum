// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use crate::error::{ForgeError, Result};

/// Cooperative cancellation, checked between reads, parser loops and stages.
/// Commit sections do not check it: they finish or leave a journal.
#[derive(Clone, Debug, Default)]
pub struct CancelToken(Arc<AtomicBool>);

impl CancelToken {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn cancel(&self) {
        self.0.store(true, Ordering::Release);
    }

    pub fn is_cancelled(&self) -> bool {
        self.0.load(Ordering::Acquire)
    }

    pub fn check(&self) -> Result<()> {
        if self.is_cancelled() {
            Err(ForgeError::cancelled())
        } else {
            Ok(())
        }
    }
}
