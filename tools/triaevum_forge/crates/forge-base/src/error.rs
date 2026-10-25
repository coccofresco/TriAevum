// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Error families shared by every frontend. The numeric values are part of
//! the C ABI (`TriAevumForgeErrorKind` in `include/triaevum_forge.h`).

use std::fmt;
use std::path::PathBuf;

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ErrorKind {
    InvalidInput = 1,
    EncryptedInput = 2,
    MissingKey = 3,
    UnsupportedFormat = 4,
    UnsupportedRevision = 5,
    IncompatibleTarget = 6,
    ArtifactIntegrity = 7,
    InvalidSettings = 8,
    StorageFull = 9,
    PermissionDenied = 10,
    Busy = 11,
    RecoveryRequired = 12,
    NativePreflight = 13,
    PreparationFailed = 14,
    Cancelled = 15,
    Internal = 16,
}

impl ErrorKind {
    pub fn code(self) -> &'static str {
        match self {
            Self::InvalidInput => "invalid_input",
            Self::EncryptedInput => "encrypted_input",
            Self::MissingKey => "missing_key",
            Self::UnsupportedFormat => "unsupported_format",
            Self::UnsupportedRevision => "unsupported_revision",
            Self::IncompatibleTarget => "incompatible_target",
            Self::ArtifactIntegrity => "artifact_integrity",
            Self::InvalidSettings => "invalid_settings",
            Self::StorageFull => "storage_full",
            Self::PermissionDenied => "permission_denied",
            Self::Busy => "busy",
            Self::RecoveryRequired => "recovery_required",
            Self::NativePreflight => "native_preflight",
            Self::PreparationFailed => "preparation_failed",
            Self::Cancelled => "cancelled",
            Self::Internal => "internal",
        }
    }
}

/// A failure with a stable family and the human message the Python Forge
/// showed for the same case. `stage` names the import stage when known.
#[derive(Debug)]
pub struct ForgeError {
    pub kind: ErrorKind,
    pub message: String,
    pub stage: Option<&'static str>,
    pub os_code: Option<i32>,
    pub paths: Vec<PathBuf>,
}

pub type Result<T> = std::result::Result<T, ForgeError>;

impl ForgeError {
    pub fn new(kind: ErrorKind, message: impl Into<String>) -> Self {
        Self { kind, message: message.into(), stage: None, os_code: None, paths: Vec::new() }
    }

    pub fn invalid(message: impl Into<String>) -> Self {
        Self::new(ErrorKind::InvalidInput, message)
    }

    pub fn integrity(message: impl Into<String>) -> Self {
        Self::new(ErrorKind::ArtifactIntegrity, message)
    }

    pub fn internal(message: impl Into<String>) -> Self {
        Self::new(ErrorKind::Internal, message)
    }

    pub fn cancelled() -> Self {
        Self::new(ErrorKind::Cancelled, "The operation was cancelled")
    }

    pub fn at_stage(mut self, stage: &'static str) -> Self {
        self.stage.get_or_insert(stage);
        self
    }

    pub fn with_path(mut self, path: impl Into<PathBuf>) -> Self {
        self.paths.push(path.into());
        self
    }

    /// Maps an I/O failure to its family, keeping the OS error code.
    pub fn io(context: impl fmt::Display, error: std::io::Error) -> Self {
        use std::io::ErrorKind as Io;
        let kind = match error.kind() {
            Io::PermissionDenied => ErrorKind::PermissionDenied,
            Io::StorageFull | Io::QuotaExceeded => ErrorKind::StorageFull,
            Io::NotFound | Io::InvalidData | Io::UnexpectedEof => ErrorKind::InvalidInput,
            _ => ErrorKind::PreparationFailed,
        };
        Self {
            kind,
            message: format!("{context}: {error}"),
            stage: None,
            os_code: error.raw_os_error(),
            paths: Vec::new(),
        }
    }
}

impl fmt::Display for ForgeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for ForgeError {}
