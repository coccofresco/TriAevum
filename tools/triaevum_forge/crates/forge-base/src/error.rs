// Copyright TriAevum contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Error families shared by every frontend. The numeric values are part of
//! the C ABI (`TriAevumForgeErrorKind` in `include/triaevum_forge.h`).
//!
//! Besides the family, an error may say what Forge was doing
//! ([`Operation`]), what the failing thing was ([`Subject`]) and why
//! ([`Reason`]). These optional facts refine the user explanation
//! (`forge_core::explain`); a consumer that does not know them falls back to
//! the family and stage. The first value set wins: the innermost site knows
//! best, and outer layers only fill what is still unknown.

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
    pub const ALL: [ErrorKind; 16] = [
        Self::InvalidInput,
        Self::EncryptedInput,
        Self::MissingKey,
        Self::UnsupportedFormat,
        Self::UnsupportedRevision,
        Self::IncompatibleTarget,
        Self::ArtifactIntegrity,
        Self::InvalidSettings,
        Self::StorageFull,
        Self::PermissionDenied,
        Self::Busy,
        Self::RecoveryRequired,
        Self::NativePreflight,
        Self::PreparationFailed,
        Self::Cancelled,
        Self::Internal,
    ];

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

    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|kind| kind.code() == code)
    }
}

/// An enum serialized as a stable snake_case code (JSON events, the ABI).
macro_rules! coded {
    ($(#[$meta:meta])* $name:ident { $($(#[$item_meta:meta])* $variant:ident = $code:literal,)+ }) => {
        $(#[$meta])*
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
        pub enum $name {
            $($(#[$item_meta])* $variant,)+
        }

        impl $name {
            pub const ALL: &'static [$name] = &[$(Self::$variant,)+];

            pub fn code(self) -> &'static str {
                match self {
                    $(Self::$variant => $code,)+
                }
            }

            pub fn from_code(code: &str) -> Option<Self> {
                Self::ALL.iter().copied().find(|item| item.code() == code)
            }
        }
    };
}

coded! {
    /// What Forge was doing.
    Operation {
        /// Parsing a frontend's job request.
        RequestValidation = "request_validation",
        LockInspection = "lock_inspection",
        /// The `rom_recognize` job.
        Recognize = "recognize",
        Spool = "spool",
        Identify = "identify",
        Extract = "extract",
        Verify = "verify",
        Repair = "repair",
        Prepare = "prepare",
        Preflight = "preflight",
        /// Checking the package's catalog, modules and runtime.
        PackageVerify = "package_verify",
        /// Asking the runtime for its product information.
        RuntimeQuery = "runtime_query",
        DeviceQuery = "device_query",
        /// Assets the installation requires (TopScreen, water, scene lights).
        RequiredAssets = "required_assets",
        InteriorSun = "interior_sun",
        /// A native helper process (shader and pipeline preparation).
        NativeHelper = "native_helper",
        OptionalDownload = "optional_download",
        OptionalImport = "optional_import",
        Install = "install",
        Update = "update",
        Restore = "restore",
        /// Replacing package files (the runtime) in the installation.
        PackageReplace = "package_replace",
        Activate = "activate",
        SettingsLoad = "settings_load",
        SettingsSave = "settings_save",
        Inspect = "inspect",
        Launch = "launch",
        /// Backing up the user's files (saves, settings).
        Backup = "backup",
        /// Moving user files to a new managed location.
        Migration = "migration",
        /// Deleting generated or cached data.
        Cleanup = "cleanup",
    }
}

coded! {
    /// What the failing thing is.
    Subject {
        /// The file or folder the user selected.
        SelectedInput = "selected_input",
        /// Data Forge installed earlier (sources, titles, receipts).
        InstalledData = "installed_data",
        /// A file the publisher shipped in the package.
        Package = "package",
        Settings = "settings",
        /// Where Forge writes.
        Destination = "destination",
        /// An optional enhancement (HD textures, a mod).
        OptionalEnhancement = "optional_enhancement",
        Runtime = "runtime",
    }
}

coded! {
    /// Why it failed: an operating-system cause kept at the source, or a
    /// verified Forge cause.
    Reason {
        NotFound = "not_found",
        AccessDenied = "access_denied",
        ReadOnlyDestination = "read_only_destination",
        StorageFull = "storage_full",
        /// Forge's estimate of the space an operation needs exceeds what is free.
        InsufficientSpace = "insufficient_space",
        PathTooLong = "path_too_long",
        /// The file is in use or read-only; the holder is not known.
        FileLocked = "file_locked",
        /// A program Windows named holds the file.
        VerifiedFileHolder = "verified_file_holder",
        TimedOut = "timed_out",
        LegacyLock = "legacy_lock",
        MalformedSettings = "malformed_settings",
        NewerSchema = "newer_schema",
        MissingModule = "missing_module",
        MissingCatalogue = "missing_catalogue",
        MissingRuntime = "missing_runtime",
        MissingRetainedSource = "missing_retained_source",
        IdentityMismatch = "identity_mismatch",
        ProviderGrantLost = "provider_grant_lost",
        MissingLoaderDependency = "missing_loader_dependency",
        UnsupportedCpu = "unsupported_cpu",
        NoCompatibleDevice = "no_compatible_device",
        StepFailed = "step_failed",
        RollbackFailure = "rollback_failure",
        AfterActivationCommitted = "after_activation_committed",
        /// The game holds this installation's session lock.
        GameRunning = "game_running",
        /// Another Forge holds this installation's lock.
        ForgeRunning = "forge_running",
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
    pub operation: Option<Operation>,
    pub subject: Option<Subject>,
    pub reason: Option<Reason>,
}

pub type Result<T> = std::result::Result<T, ForgeError>;

impl ForgeError {
    pub fn new(kind: ErrorKind, message: impl Into<String>) -> Self {
        Self {
            kind,
            message: message.into(),
            stage: None,
            os_code: None,
            paths: Vec::new(),
            operation: None,
            subject: None,
            reason: None,
        }
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

    /// Records the operation unless an inner site already did.
    pub fn during(mut self, operation: Operation) -> Self {
        self.operation.get_or_insert(operation);
        self
    }

    /// Records the subject unless an inner site already did.
    pub fn about(mut self, subject: Subject) -> Self {
        self.subject.get_or_insert(subject);
        self
    }

    /// Records the reason unless an inner site already did.
    pub fn because(mut self, reason: Reason) -> Self {
        self.reason.get_or_insert(reason);
        self
    }

    /// Takes over the structured facts of the error this one replaces (a
    /// wrapper with its own family and message keeps the original cause).
    pub fn caused_by(mut self, cause: &ForgeError) -> Self {
        self.os_code = self.os_code.or(cause.os_code);
        self.operation = self.operation.or(cause.operation);
        self.subject = self.subject.or(cause.subject);
        self.reason = self.reason.or(cause.reason);
        self
    }

    /// Maps an I/O failure to its family, keeping the OS error code and
    /// the cause it names.
    pub fn io(context: impl fmt::Display, error: std::io::Error) -> Self {
        use std::io::ErrorKind as Io;
        let kind = match error.kind() {
            Io::PermissionDenied => ErrorKind::PermissionDenied,
            Io::StorageFull | Io::QuotaExceeded => ErrorKind::StorageFull,
            Io::NotFound | Io::InvalidData | Io::UnexpectedEof => ErrorKind::InvalidInput,
            _ => ErrorKind::PreparationFailed,
        };
        Self {
            os_code: error.raw_os_error(),
            reason: os_reason(&error),
            ..Self::new(kind, format!("{context}: {error}"))
        }
    }
}

/// The cause an I/O error names, independently of the family it maps to.
pub fn os_reason(error: &std::io::Error) -> Option<Reason> {
    use std::io::ErrorKind as Io;
    // Windows: ERROR_WRITE_PROTECT, ERROR_SHARING_VIOLATION,
    // ERROR_LOCK_VIOLATION, ERROR_FILENAME_EXCED_RANGE. POSIX ENAMETOOLONG
    // is InvalidFilename.
    if cfg!(windows) {
        match error.raw_os_error() {
            Some(19) => return Some(Reason::ReadOnlyDestination),
            Some(32 | 33) => return Some(Reason::FileLocked),
            Some(206) => return Some(Reason::PathTooLong),
            _ => {}
        }
    }
    Some(match error.kind() {
        Io::NotFound => Reason::NotFound,
        Io::PermissionDenied => Reason::AccessDenied,
        Io::ReadOnlyFilesystem => Reason::ReadOnlyDestination,
        Io::StorageFull | Io::QuotaExceeded => Reason::StorageFull,
        Io::InvalidFilename if cfg!(unix) => Reason::PathTooLong,
        Io::ResourceBusy | Io::ExecutableFileBusy => Reason::FileLocked,
        Io::TimedOut => Reason::TimedOut,
        _ => return None,
    })
}

impl fmt::Display for ForgeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for ForgeError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_values_are_stable() {
        let values: Vec<u32> = ErrorKind::ALL.iter().map(|kind| *kind as u32).collect();
        assert_eq!(values, (1..=16).collect::<Vec<u32>>());
        for kind in ErrorKind::ALL {
            assert_eq!(ErrorKind::from_code(kind.code()), Some(kind));
        }
    }

    #[test]
    fn codes_round_trip_and_are_unique() {
        let mut seen = std::collections::HashSet::new();
        for code in Operation::ALL.iter().map(|item| item.code()) {
            assert!(seen.insert(("operation", code)));
            assert_eq!(Operation::from_code(code).unwrap().code(), code);
        }
        for code in Reason::ALL.iter().map(|item| item.code()) {
            assert!(seen.insert(("reason", code)));
            assert_eq!(Reason::from_code(code).unwrap().code(), code);
        }
        for code in Subject::ALL.iter().map(|item| item.code()) {
            assert!(seen.insert(("subject", code)));
            assert_eq!(Subject::from_code(code).unwrap().code(), code);
        }
        assert_eq!(Operation::from_code("unknown"), None);
    }

    #[test]
    fn io_errors_keep_their_cause() {
        let error = ForgeError::io("Cannot write x", std::io::Error::from(std::io::ErrorKind::ReadOnlyFilesystem));
        assert_eq!(error.kind, ErrorKind::PreparationFailed);
        assert_eq!(error.reason, Some(Reason::ReadOnlyDestination));
        let denied = ForgeError::io("x", std::io::Error::from(std::io::ErrorKind::PermissionDenied));
        assert_eq!((denied.kind, denied.reason), (ErrorKind::PermissionDenied, Some(Reason::AccessDenied)));
        let wrapped =
            ForgeError::new(ErrorKind::NativePreflight, "wrapped").caused_by(&denied).because(Reason::StepFailed);
        assert_eq!(wrapped.reason, Some(Reason::AccessDenied));
        assert_eq!(wrapped.kind, ErrorKind::NativePreflight);
    }
}
