"""Resolve public-tool data identically from source and frozen Forge builds."""

from __future__ import annotations

import sys
from pathlib import Path

try:
    from .host_layout import HostLayout, for_package
except ImportError:
    from host_layout import HostLayout, for_package


def distribution_root() -> Path:
    frozen_root = getattr(sys, "_MEIPASS", None)
    if frozen_root:
        return Path(frozen_root).resolve()
    return Path(__file__).resolve().parents[2]


def distribution_path(relative: str) -> Path:
    return distribution_root() / Path(relative)


def installation_root() -> Path:
    """Return the public package root beside a frozen Forge executable."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return distribution_root()


def installation_path(relative: str) -> Path:
    return installation_root() / Path(relative)


def installation_layout() -> HostLayout:
    return for_package(installation_root())


def activation_path(relative: str) -> Path:
    return installation_layout().activation / Path(relative)
