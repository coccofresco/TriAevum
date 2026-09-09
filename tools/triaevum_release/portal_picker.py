"""One cancellable file-portal subprocess, independent of the Tk UI thread."""

from pathlib import Path
import subprocess
import threading
from urllib.parse import urlsplit
from urllib.request import url2pathname

try:
    from .native_process import native_process_environment
except ImportError:
    from native_process import native_process_environment


def selected_path(output: str) -> Path:
    uri = output.rstrip("\r\n")
    if not uri or "\n" in uri or "\r" in uri or "\0" in uri:
        raise ValueError("File portal returned more than one selection or an invalid URI")
    parsed = urlsplit(uri)
    if (parsed.scheme != "file" or parsed.netloc not in ("", "localhost")
            or parsed.query or parsed.fragment):
        raise ValueError("File portal did not return a local file")
    decoded = url2pathname(parsed.path)
    path = Path(decoded)
    if "\0" in decoded or not path.is_absolute():
        raise ValueError("File portal returned an invalid absolute path")
    return path


def choose(helper: Path, *, title: str, directory: bool, parent: str,
           closing: threading.Event) -> Path | None:
    command = [str(helper), "--title", title, "--parent", parent]
    if directory:
        command.append("--directory")
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, encoding="utf-8", env=native_process_environment()) as process:
        while True:
            try:
                output, error = process.communicate(timeout=0.1)
                break
            except subprocess.TimeoutExpired:
                if closing.is_set():
                    process.terminate()
                    try:
                        process.communicate(timeout=12)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate()
                    return None
        if process.returncode == 2:
            return None
        if process.returncode != 0:
            raise RuntimeError(error.strip() or "The desktop file portal closed or timed out")
        return selected_path(output)
