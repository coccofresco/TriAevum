# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs verified_download.download_verified and romfs_mods.download_archive over
scripted transports (as test_verified_download.py and test_romfs_mods.py do)
and writes the outcomes, for comparison with the Rust port.

usage: download_cases.py work_dir result.json
"""

import hashlib
import io
import json
import sys
import urllib.error
from pathlib import Path
from unittest.mock import patch

RELEASE = Path("I:/TriAevum-devwarp/tools/triaevum_release")
sys.path.insert(0, str(RELEASE))
sys.path.insert(0, str(RELEASE.parents[1]))

import dataclasses  # noqa: E402

import romfs_mods  # noqa: E402
from romfs_mods import ArchiveContract  # noqa: E402
from verified_download import download_verified  # noqa: E402

DATA = b"expected archive"
SHA = hashlib.sha256(DATA).hexdigest()
URL = "https://example.test/archive"


def response(data, url=URL):
    stream = io.BytesIO(data)
    stream.geturl = lambda: url
    return stream


def http_error(code):
    return urllib.error.HTTPError("https://example.test", code, "CDN error", {}, None)


def run_verified(work, name, script, *, url=URL, size=len(DATA), sha=SHA, attempts=3, max_seconds=900,
                 existing=None):
    folder = work / name
    folder.mkdir(parents=True)
    output = folder / "archive.zip"
    if existing is not None:
        output.write_bytes(existing)
    retries = []
    with patch("verified_download.urllib.request.urlopen", side_effect=script) as fetch, \
            patch("verified_download.time.sleep"):
        try:
            download_verified(url, output, size=size, sha256=sha, retry=lambda attempt, error: retries.append(attempt),
                              attempts=attempts, max_seconds=max_seconds)
            error = None
        except Exception as exc:
            error = str(exc)
        calls = fetch.call_count
    return {"error": error, "calls": calls, "retries": retries,
            "file": output.read_bytes().hex() if output.is_file() else None,
            "leftover": sorted(path.name for path in folder.iterdir() if path.name != "archive.zip")}


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()


def run_archive(work, name, bodies, payload=b"original archive bytes"):
    folder = work / name
    contract = ArchiveContract("vanilla_plus_link_1_9_archive", "fixture.rar", len(payload),
                               hashlib.sha256(payload).hexdigest())
    mod = dataclasses.replace(romfs_mods.VANILLA_PLUS_LINK, downloads=(
        "https://example.invalid/primary", "https://example.invalid/mirror"))
    requested, reports, progress = [], [], []
    queue = list(bodies)

    def opener(request, *, timeout, context):
        requested.append(request.full_url)
        return FakeResponse(queue.pop(0))

    try:
        path = romfs_mods.download_archive(folder, mod, contract, opener=opener, report=reports.append,
                                           progress=lambda done, total: progress.append([done, total]))
        error = None
    except Exception as exc:
        path, error = None, str(exc)
    downloads = folder / "downloads"
    return {"error": error, "requested": requested, "reports": reports, "progress": progress,
            "file": path.read_bytes().hex() if path else None,
            "leftover": sorted(p.name for p in downloads.iterdir()) if downloads.exists() else []}


def main(work, output):
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    results = {}
    results["retry_then_success"] = run_verified(work, "a", [TimeoutError("slow CDN"), response(b"short"),
                                                             response(DATA)])
    results["integrity"] = run_verified(work, "b", [response(b"x" * len(DATA))], existing=b"old cache")
    for code in (503, 429, 403, 404, 500):
        results[f"status_{code}"] = run_verified(work, f"c{code}", [http_error(code)] * 3)
    results["insecure_redirect"] = run_verified(work, "d", [response(DATA, "http://example.test")])
    results["transport_fails"] = run_verified(work, "e", [TimeoutError("slow")] * 3)
    results["one_attempt"] = run_verified(work, "f", [TimeoutError("slow")], attempts=1)
    results["too_long"] = run_verified(work, "g", [response(DATA + b"extra")])
    results["incomplete"] = run_verified(work, "h", [response(b"short"), response(b"short"), response(b"short")])
    results["bad_contract_http"] = run_verified(work, "i", [], url="http://example.test/archive")
    results["bad_contract_size"] = run_verified(work, "j", [], size=0)
    results["bad_contract_sha"] = run_verified(work, "k", [], sha=SHA.upper())
    results["bad_contract_attempts"] = run_verified(work, "l", [], attempts=4)
    results["bad_contract_seconds"] = run_verified(work, "m", [], max_seconds=0)
    results["deadline"] = run_verified(work, "n", [], attempts=1, max_seconds=1e-9)

    payload = b"original archive bytes"
    results["archive_fallback"] = run_archive(work, "p", [b"<html>challenge</html>", payload])
    results["archive_first_ok"] = run_archive(work, "q", [payload])
    results["archive_all_fail"] = run_archive(work, "r", [payload + b"newer", b"short"])
    results["archive_both_ok_second_unused"] = run_archive(work, "s", [payload, payload])
    Path(output).write_text(json.dumps(results, sort_keys=True, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
