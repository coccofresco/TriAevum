"""Recoverable Forge publication, separate from title generation and rendering."""

import os
import tempfile
from contextlib import contextmanager
from pathlib import Path

try:
    from .cache_lock import acquire_cache_lock, release_cache_lock
    from .common import atomic_write_json, load_json_object, sha256_file
except ImportError:
    from cache_lock import acquire_cache_lock, release_cache_lock
    from common import atomic_write_json, load_json_object, sha256_file


def _copy_synced(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=".activation-", dir=target.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as stream:
            while chunk := stream.read(4 * 1024 * 1024):
                output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def journal_path(installation: Path) -> Path:
    return installation / ".forge-activation" / "journal.json"


@contextmanager
def installation_lock(installation: Path):
    installation.mkdir(parents=True, exist_ok=True)
    descriptor = acquire_cache_lock(installation / ".forge-activation.lock")
    try:
        yield
    finally:
        release_cache_lock(descriptor)


def _restore(journal: Path, targets: list[Path]) -> None:
    payload = load_json_object(journal)
    entries = payload.get("entries")
    if (payload.get("format") != "triaevum_activation_v1"
            or not isinstance(entries, list)
            or len(entries) != len(targets)):
        raise ValueError("Invalid Forge activation recovery journal")
    # Validate the entire recovery set before writing anything. A changed target
    # set requires explicit recovery with the original installation, not guessing.
    for index, (entry, target) in enumerate(zip(entries, targets)):
        if not isinstance(entry, dict) or entry.get("target") != str(target):
            raise ValueError("Pending activation targets differ; retry the original title installation")
        digest = entry.get("sha256")
        if digest is not None and sha256_file(journal.parent / f"{index}.bak") != digest:
            raise ValueError("Forge activation backup is damaged")
    for index, (entry, target) in enumerate(zip(entries, targets)):
        if entry["sha256"] is None:
            target.unlink(missing_ok=True)
        elif not target.is_file() or sha256_file(target) != entry["sha256"]:
            _copy_synced(journal.parent / f"{index}.bak", target)
    journal.unlink()


@contextmanager
def activation_transaction(installation: Path, targets: list[Path]):
    targets = [target.resolve() for target in targets]
    if len(set(targets)) != len(targets):
        raise ValueError("Duplicate activation target")
    journal = journal_path(installation)
    with installation_lock(installation):
        journal.parent.mkdir(parents=True, exist_ok=True)
        if journal.exists():
            _restore(journal, targets)
        entries = []
        for index, target in enumerate(targets):
            if target.exists():
                backup = journal.parent / f"{index}.bak"
                # Stream potentially large private DLLs; do not duplicate them in RAM.
                _copy_synced(target, backup)
                digest = sha256_file(backup)
            else:
                digest = None
            entries.append({"target": str(target), "sha256": digest})
        atomic_write_json(journal, {"format": "triaevum_activation_v1", "entries": entries})
        try:
            yield
        except BaseException:
            _restore(journal, targets)
            raise
        else:
            journal.unlink()
        finally:
            # Retain backups if rollback failed, so the next Forge can recover.
            if not journal.exists():
                for index in range(len(targets)):
                    (journal.parent / f"{index}.bak").unlink(missing_ok=True)
