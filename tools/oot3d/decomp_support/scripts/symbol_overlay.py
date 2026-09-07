#!/usr/bin/env python3
"""Apply manual-symbol names without requiring a fresh Ghidra export."""

from __future__ import annotations

import csv
import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
IDENT_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")


@dataclass(frozen=True)
class ManualSymbol:
    entry: str
    old_name: str
    new_name: str
    kind: str
    confidence: str
    source_file: str
    notes: str


def normalize_entry(entry: str) -> str:
    value = entry.strip().lower().removeprefix("0x")
    if len(value) != 8:
        raise ValueError(f"entry must be 8 hex digits: {entry}")
    int(value, 16)
    return value


def load_manual_symbols(path: Path = MANUAL_SYMBOLS) -> list[ManualSymbol]:
    if not path.is_file():
        return []
    symbols: list[ManualSymbol] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            symbols.append(
                ManualSymbol(
                    entry=normalize_entry(row["entry"]),
                    old_name=row.get("old_name", ""),
                    new_name=row["new_name"],
                    kind=row.get("kind", ""),
                    confidence=row.get("confidence", ""),
                    source_file=row.get("source_file", ""),
                    notes=row.get("notes", ""),
                )
            )
    return symbols


def manual_entries(symbols: list[ManualSymbol]) -> set[str]:
    return {symbol.entry for symbol in symbols}


def names_by_entry(symbols: list[ManualSymbol]) -> dict[str, str]:
    return {symbol.entry: symbol.new_name for symbol in symbols}


def identifier_overlay(symbols: list[ManualSymbol]) -> dict[str, str]:
    overlay: dict[str, str] = {}
    for symbol in symbols:
        for name in {symbol.old_name, symbol.new_name, f"FUN_{symbol.entry}"}:
            if name:
                overlay[name] = symbol.new_name
    return overlay


def overlay_identifier_text(text: str, overlay: dict[str, str]) -> str:
    if not overlay:
        return text

    def replace(match: re.Match[str]) -> str:
        token = match.group(0)
        return overlay.get(token, token)

    return IDENT_RE.sub(replace, text)


def overlay_name(entry: str, fallback: str, symbols_by_entry: dict[str, str]) -> str:
    return symbols_by_entry.get(normalize_entry(entry), fallback)


def overlay_calls(calls: set[str] | list[str], overlay: dict[str, str]) -> list[str]:
    return sorted({overlay.get(str(call), str(call)) for call in calls})
