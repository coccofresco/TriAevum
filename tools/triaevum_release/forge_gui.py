"""Minimal guided desktop frontend for TriAevum Forge."""

from __future__ import annotations

import ctypes
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import uuid
from dataclasses import dataclass
import time
from pathlib import Path
from typing import Any, Callable

try:
    from . import ctr_rom, forge, extracted_inputs, game_language
    from .bundle_paths import installation_path
    from .common import load_json_object
    from .installed_runtime import validate_installed_runtime
    from .activation_transaction import installation_lock, journal_path
    from .installation_context import resolve_reference
    from .precompiled_titles import load_catalog, select_title, install_precompiled_title
    from .input_adapters import import_contract, adapt_extracted_inputs
    from .release_platform import host_platform
    from .native_process import popen_native
    from .host_layout import for_package
    from . import portal_picker
except ImportError:
    import ctr_rom
    import forge
    import extracted_inputs
    import game_language
    from bundle_paths import installation_path
    from common import load_json_object
    from installed_runtime import validate_installed_runtime
    from activation_transaction import installation_lock, journal_path
    from installation_context import resolve_reference
    from precompiled_titles import load_catalog, select_title, install_precompiled_title
    from input_adapters import import_contract, adapt_extracted_inputs
    from release_platform import host_platform
    from native_process import popen_native
    from host_layout import for_package
    import portal_picker


StageReporter = Callable[[str, str], None]


@dataclass(frozen=True)
class RecipeOption:
    recipe_id: str
    label: str


@dataclass(frozen=True)
class InstallRequest:
    rom: Path
    data_root: Path | None = None


@dataclass(frozen=True)
class ActiveTitle:
    recipe_id: str
    directory: Path
    module_sha256: str


def load_recipe_options(path: Path = forge.DEFAULT_RECIPES) -> tuple[RecipeOption, ...]:
    payload = load_json_object(path)
    if payload.get("format") != "triaevum_supported_revisions_v1":
        raise forge.ForgeError("unsupported supported-revision recipe format")
    recipes = payload.get("recipes")
    if not isinstance(recipes, list):
        raise forge.ForgeError("supported-revision recipe list is malformed")

    options: list[RecipeOption] = []
    for item in recipes:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            continue
        title = str(item.get("title") or item["id"])
        region = str(item.get("region") or "").strip()
        label = f"{title} ({region})" if region else title
        options.append(RecipeOption(str(item["id"]), label))
    if not options:
        raise forge.ForgeError("no supported title revisions are available")
    return tuple(options)


def match_extracted_recipe(
    extracted: ctr_rom.ExtractedTitleInputs,
    path: Path = forge.DEFAULT_RECIPES,
) -> dict[str, Any]:
    payload = load_json_object(path)
    if payload.get("format") != "triaevum_supported_revisions_v1":
        raise forge.ForgeError("unsupported supported-revision recipe format")
    recipes = payload.get("recipes")
    if not isinstance(recipes, list):
        raise forge.ForgeError("supported-revision recipe list is malformed")

    matches: list[dict[str, Any]] = []
    actual = extracted.by_kind()
    for recipe in recipes:
        if not isinstance(recipe, dict):
            continue
        contracts = import_contract(recipe)
        if not isinstance(contracts, dict):
            continue
        if all(
            isinstance(contracts.get(kind), dict)
            and contracts[kind].get("bytes") == item.bytes
            and str(contracts[kind].get("sha256", "")).lower() == item.sha256
            for kind, item in actual.items()
        ):
            matches.append(recipe)
    if not matches:
        try:
            from .data_compatibility import expected, identity
        except ImportError:
            from data_compatibility import expected, identity
        candidates = [recipe for recipe in recipes if isinstance(recipe, dict)
                      and expected(recipe, 'source') is not None
                      and expected(recipe, 'source')['code'] == {
                          'bytes': extracted.code.bytes, 'sha256': extracted.code.sha256}]
        if candidates:
            actual_family = identity({kind: item.path for kind, item in actual.items()})
            matches = [recipe for recipe in candidates if expected(recipe, 'source') == actual_family]
    if not matches:
        raise forge.ForgeError(
            "the decrypted ROM does not match any supported game revision "
            f"(program ID {extracted.program_id:016X})"
        )
    if len(matches) != 1:
        raise forge.ForgeError("the decrypted ROM matches multiple revision recipes")
    return matches[0]


def default_gui_data_root() -> Path:
    return forge.default_output_root().parent


def load_active_title(
    path: Path | None = None,
) -> ActiveTitle | None:
    state_path = (path or (default_gui_data_root() / "active-title.json")).resolve()
    if not state_path.is_file():
        return None
    try:
        active = load_json_object(state_path)
        directory = resolve_reference(str(active.get("directory", "")), state_path.parent)
        prepared = forge.load_prepared_content(directory, required_inputs=())
    except (OSError, ValueError, forge.ForgeError):
        return None
    module = prepared.state.get("module")
    if (
        active.get("format") != "triaevum_active_title_v1"
        or not isinstance(module, dict)
        or module.get("status") != "ready"
        or active.get("module_sha256") != module.get("sha256")
    ):
        return None
    return ActiveTitle(
        recipe_id=str(active.get("recipe", "")),
        directory=directory,
        module_sha256=str(active.get("module_sha256", "")),
    )


def install_private_title(
    request: InstallRequest,
    *,
    report: StageReporter = lambda _stage, _message: None,
) -> dict[str, Any]:
    started = time.perf_counter()
    data_root = (request.data_root or default_gui_data_root()).expanduser().resolve()
    data_root.mkdir(parents=True, exist_ok=True)
    output_root = data_root / "titles"
    staging = data_root / f".rom-import-{uuid.uuid4().hex}"
    try:
        report("preflight", "Checking the playable runtime and precompiled title catalog...")
        forge.query_product(runtime_path())
        root = runtime_path().parent
        catalog = load_catalog(root)
        report("extract", "Extracting native title data from the decrypted ROM...")
        if request.rom.expanduser().is_dir():
            extracted = extracted_inputs.stage_directory(request.rom, staging)
        else:
            extracted = ctr_rom.extract_decrypted_rom(request.rom, staging)

        report("verify", "Identifying and verifying the supported game revision...")
        recipe = match_extracted_recipe(extracted)
        select_title(root, recipe, catalog=catalog)
        adaptation = None
        if recipe.get("input_adapter") is not None:
            report("adapt", "Adapting this ROM for the existing title module (no compilation)...")
            extracted, adaptation = adapt_extracted_inputs(
                extracted, recipe, root=root, output=staging / "normalized")
        languages = game_language.discover(runtime_path(), extracted.romfs.path)
        extracted = ctr_rom.publish_extracted_inputs(extracted, data_root / "sources")
        cache = forge.HashCache(output_root / ".hash-cache.json")
        for item in extracted.by_kind().values():
            cache.remember(item.path, item.sha256)
        verified, verified_mod = forge.verify_sources(
            recipe,
            code_path=extracted.code.path,
            exheader_path=extracted.exheader.path,
            romfs_path=extracted.romfs.path,
            cache=cache,
        )

        report("prepare", "Preparing the private local title index...")
        prepared = forge.prepare_content(
            recipe,
            verified,
            output_root=output_root,
            verified_mod=verified_mod,
        )
        prepared_directory = Path(str(prepared["directory"])).resolve()

        report(
            "activate",
            "Installing the verified precompiled title...",
        )
        built = install_precompiled_title(
            prepared_directory,
            root=root,
            recipe=recipe,
            active_title_state=data_root / "active-title.json",
            runtime_plugin=forge.default_runtime_plugin_path(),
            launch_profile=forge.default_runtime_launch_profile_path(),
            data_root=data_root,
            report=report,
        )
        game_language.install(data_root, languages)
    finally:
        if staging.exists():
            shutil.rmtree(staging, ignore_errors=True)

    report("ready", "The title is ready to play.")
    return {
        "status": "ready",
        "recipe": recipe["id"],
        "prepared": prepared,
        "build": built,
        "adaptation": adaptation,
        "install_wall_seconds": time.perf_counter() - started,
    }


def runtime_path() -> Path:
    return installation_path(host_platform().runtime).resolve()


def launch_runtime(data_root: Path | None = None) -> subprocess.Popen[bytes]:
    try:
        layout = for_package(runtime_path().parent)
        with installation_lock(layout.activation):
            if journal_path(layout.activation).exists():
                raise forge.ForgeError("An activation was interrupted; run Forge again before playing")
            return _launch_runtime_locked(data_root)
    except (OSError, ValueError) as exc:
        raise forge.ForgeError(f"Cannot launch the installed title: {exc}") from exc


def _launch_runtime_locked(data_root: Path | None) -> subprocess.Popen[bytes]:
    executable = runtime_path()
    if not executable.is_file():
        raise forge.ForgeError(f"TriAevum runtime is missing: {executable}")
    private_root = (data_root or default_gui_data_root()).expanduser().resolve()
    active = load_active_title(private_root / "active-title.json")
    if active is None:
        raise forge.ForgeError("No valid active title in the selected data folder; run Forge first")
    try:
        prepared = forge.load_prepared_content(active.directory, required_inputs=())
        runtime = prepared.state.get("runtime")
        if not isinstance(runtime, dict):
            raise ValueError("The active title has no playable runtime receipt")
        profile = validate_installed_runtime(executable, active.directory, private_root, runtime)
    except (OSError, ValueError) as exc:
        raise forge.ForgeError(str(exc)) from exc
    return popen_native(
        [str(executable), "--launch-profile", str(profile)],
        cwd=for_package(executable.parent).activation,
    )


def _hide_explorer_console() -> None:
    if os.name != "nt":
        return
    try:
        kernel32 = ctypes.windll.kernel32
        user32 = ctypes.windll.user32
        process_ids = (ctypes.c_ulong * 2)()
        if kernel32.GetConsoleProcessList(process_ids, len(process_ids)) == 1:
            console = kernel32.GetConsoleWindow()
            if console:
                user32.ShowWindow(console, 0)
    except (AttributeError, OSError):
        pass


class ForgeWindow:
    def __init__(self, root: Any, tk: Any, ttk: Any, filedialog: Any, messagebox: Any):
        self.root = root
        self.tk = tk
        self.ttk = ttk
        self.filedialog = filedialog
        self.messagebox = messagebox
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.busy = False
        self.picking = False
        self.picker_thread = None
        self.worker_process = None
        self.closing = threading.Event()

        self.root.title("TriAevum Forge")
        self.root.geometry("760x390")
        self.root.minsize(760, 390)
        self.root.protocol("WM_DELETE_WINDOW", self._close)

        self.data_root = default_gui_data_root()
        self.rom = tk.StringVar()
        self.language = tk.StringVar()
        self.language_options = []
        self.status = tk.StringVar(value="Checking the local installation...")

        self._build_layout()
        self.rom.trace_add("write", lambda *_args: self._refresh_actions())
        self._refresh_active_title()
        self._refresh_actions()
        self.status.trace_add("write", lambda *_args: self.root.after_idle(self._fit_to_content))
        self.root.after_idle(self._fit_to_content)
        self.root.after(100, self._poll_events)

    def _fit_to_content(self) -> None:
        # Native Tk themes/font metrics and wrapped status text vary by host.
        # Never place the primary actions below a fixed Windows-sized window.
        self.root.update_idletasks()
        width = max(760, self.root.winfo_reqwidth())
        height = max(390, self.root.winfo_reqheight())
        self.root.minsize(width, height)
        if self.root.winfo_width() < width or self.root.winfo_height() < height:
            self.root.geometry(f"{max(width, self.root.winfo_width())}x{max(height, self.root.winfo_height())}")

    def _build_layout(self) -> None:
        outer = self.ttk.Frame(self.root, padding=20)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(1, weight=1)

        title = self.ttk.Label(
            outer, text="TriAevum Forge", font=("Segoe UI", 18, "bold")
        )
        title.grid(row=0, column=0, columnspan=3, sticky="w")
        subtitle = self.ttk.Label(
            outer,
            text=(
                "Select your own decrypted Nintendo 3DS cartridge image. Forge "
                "accepts .3ds and .cci ROMs, extracts the required native data, "
                "and does not handle encryption keys. You can also select a folder "
                "containing decompressed code.bin, exheader.bin and romfs.bin.\n\n"
                "Forge will download the official TopScreen package and install its "
                "modified HUD and menu textures for correct single-screen functionality. "
                "Internet access is needed unless a verified local copy is already available."
            ),
            wraplength=700,
            justify="left",
        )
        subtitle.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(4, 18))

        self._file_row(
            outer,
            2,
            "Decrypted ROM",
            self.rom,
            "Select a decrypted .3ds or .cci ROM",
            filetypes=(
                ("Nintendo 3DS ROM", "*.3ds *.cci"),
                ("3DS image", "*.3ds"),
                ("CCI image", "*.cci"),
            ),
        )
        private_path = self.ttk.Label(
            outer,
            text=f"Private generated data: {self.data_root}",
            wraplength=700,
            justify="left",
        )
        private_path.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(5, 0))
        self.ttk.Button(
            outer, text="Use extracted data...", command=self._browse_extracted
        ).grid(row=4, column=0, columnspan=3, sticky="w", pady=(5, 0))

        self.ttk.Label(outer, text="Game language").grid(row=5, column=0, sticky="w")
        self.language_combo = self.ttk.Combobox(outer, textvariable=self.language, state="disabled")
        self.language_combo.grid(row=5, column=1, columnspan=2, sticky="ew", pady=(10, 0))
        self.language_combo.bind("<<ComboboxSelected>>", self._select_language)
        separator = self.ttk.Separator(outer)
        separator.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(18, 14))

        self.status_label = self.ttk.Label(
            outer, textvariable=self.status, wraplength=700, justify="left"
        )
        self.status_label.grid(row=7, column=0, columnspan=3, sticky="ew")
        self.progress = self.ttk.Progressbar(outer, mode="indeterminate")
        self.progress.grid(row=8, column=0, columnspan=3, sticky="ew", pady=(10, 18))

        actions = self.ttk.Frame(outer)
        actions.grid(row=9, column=0, columnspan=3, sticky="ew")
        actions.columnconfigure(0, weight=1)
        self.install_button = self.ttk.Button(
            actions, text="Prepare and install", command=self._install
        )
        self.install_button.grid(row=0, column=1, padx=(8, 0))
        self.launch_button = self.ttk.Button(
            actions, text="Launch game", command=self._launch
        )
        self.launch_button.grid(row=0, column=2, padx=(8, 0))

    def _browse_extracted(self) -> None:
        if self.busy or self.picking:
            return
        if for_package(runtime_path().parent).use_file_portal:
            self._portal_browse(self.rom, "Select extracted title data", directory=True)
            return
        selected = self.filedialog.askdirectory(title="Select extracted title data", parent=self.root)
        if selected:
            self.rom.set(selected)
            self._refresh_actions()

    def _file_row(
        self,
        parent: Any,
        row: int,
        label: str,
        variable: Any,
        dialog_title: str,
        *,
        filetypes: tuple[tuple[str, str], ...] | None = None,
    ) -> None:
        self.ttk.Label(parent, text=label).grid(
            row=row, column=0, sticky="w", padx=(0, 12), pady=5
        )
        entry = self.ttk.Entry(parent, textvariable=variable)
        entry.grid(row=row, column=1, sticky="ew", pady=5)
        button = self.ttk.Button(
            parent,
            text="Browse...",
            command=lambda: self._browse(variable, dialog_title, filetypes),
        )
        button.grid(row=row, column=2, padx=(8, 0), pady=5)

    def _browse(
        self,
        variable: Any,
        title: str,
        filetypes: tuple[tuple[str, str], ...] | None,
    ) -> None:
        if self.busy or self.picking:
            return
        if for_package(runtime_path().parent).use_file_portal:
            self._portal_browse(variable, title, directory=False)
            return
        arguments: dict[str, Any] = {"title": title, "parent": self.root}
        if filetypes is not None:
            arguments["filetypes"] = filetypes
        selected = self.filedialog.askopenfilename(**arguments)
        if selected:
            variable.set(selected)

    def _portal_browse(self, variable: Any, title: str, *, directory: bool) -> None:
        self.picking = True
        self._refresh_actions()
        # Tk on Linux owns an X11 window, including under XWayland.
        parent = f"x11:{self.root.winfo_id():x}"
        helper = installation_path("triaevum-file-chooser")

        def worker() -> None:
            try:
                selected = portal_picker.choose(helper, title=title, directory=directory,
                                                parent=parent, closing=self.closing)
                self.events.put(("selection", (variable, selected, None)))
            except Exception as exc:
                self.events.put(("selection", (variable, None, str(exc))))
        self.picker_thread = threading.Thread(target=worker, name="TriAevumFilePortal", daemon=True)
        self.picker_thread.start()

    def _request(self) -> InstallRequest:
        return InstallRequest(
            rom=Path(self.rom.get().strip()),
            data_root=self.data_root,
        )

    def _refresh_active_title(self) -> None:
        active = load_active_title(self.data_root / "active-title.json")
        if active is None:
            self.active_title = None
            self.status.set("No active title is installed. Select a decrypted ROM.")
        else:
            self.active_title = active
            self.status.set(f"Ready to play: {active.recipe_id}\n{active.directory}")
        self._refresh_languages()

    def _refresh_languages(self) -> None:
        self.language_options = []
        path = game_language.config_path(self.data_root)
        if path.exists():
            try:
                document = game_language.validate(load_json_object(path))
                self.language_options = document["available"]
                self.language_combo.configure(values=[v["label"] for v in self.language_options])
                self.language.set(next(v["label"] for v in self.language_options if v["code"] == document["selected"]))
            except (OSError, ValueError) as exc:
                self.status.set(f"Cannot read game language settings: {exc}")
        if not self.language_options:
            self.language.set("Available after ROM preparation")

    def _select_language(self, _event=None) -> None:
        if self.busy or self.picking:
            return
        try:
            code = next(v["code"] for v in self.language_options if v["label"] == self.language.get())
            game_language.select(self.data_root, code)
            self.status.set("Game language saved. Applies on the next full game start.")
        except (OSError, ValueError, StopIteration) as exc:
            self.messagebox.showerror("TriAevum Forge", str(exc), parent=self.root)
            self._refresh_languages()

    def _refresh_actions(self) -> None:
        self.language_combo.configure(state="readonly" if self.language_options and not self.busy and not self.picking else "disabled")
        complete = bool(self.rom.get().strip())
        self.install_button.configure(
            state="normal" if complete and not self.busy and not self.picking else "disabled"
        )
        self.launch_button.configure(
            state="normal"
            if self.active_title is not None and not self.busy and not self.picking
            else "disabled"
        )

    def _install(self) -> None:
        if self.busy or self.picking:
            return
        request = self._request()
        self.busy = True
        self.progress.start(12)
        self.status.set("Starting title preparation...")
        self._refresh_actions()

        def worker() -> None:
            try:
                command = [sys.executable]
                if not getattr(sys, "frozen", False):
                    command += ["-m", "tools.triaevum_release.forge_entry"]
                command += ["--install-worker", "--rom", str(request.rom.resolve()),
                            "--data-root", str(self.data_root.resolve()), "--owner-pid", str(os.getpid())]
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, encoding="utf-8", errors="replace",
                    cwd=installation_path(""),
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                self.worker_process = process
                if self.closing.is_set():
                    process.kill()
                terminal = None
                for line in process.stdout:
                    if self.closing.is_set():
                        process.kill()
                        break
                    try:
                        event = json.loads(line)
                    except ValueError:
                        continue
                    if isinstance(event, dict) and event.get("forge_worker_event") in ("stage", "complete", "error"):
                        name = event["forge_worker_event"]
                        if name in ("complete", "error"):
                            terminal = (name, event["payload"])
                        else:
                            self.events.put((name, event["payload"]))
                code = process.wait()
                process.stdout.close()
                self.worker_process = None
                if terminal and not self.closing.is_set():
                    if code and terminal[0] == "complete":
                        raise RuntimeError(f"Forge installation worker failed after reporting success ({code})")
                    self.events.put(terminal)
                if not terminal and not self.closing.is_set():
                    raise RuntimeError(f"Forge installation worker exited without a result ({code})")
            except Exception as exc:
                self.events.put(("error", str(exc)))
            finally:
                self.worker_process = None

        threading.Thread(target=worker, name="TriAevumForge", daemon=True).start()

    def _launch(self) -> None:
        try:
            launch_runtime(self.data_root)
        except (OSError, forge.ForgeError) as exc:
            self.messagebox.showerror("TriAevum Forge", str(exc), parent=self.root)

    def _poll_events(self) -> None:
        try:
            while True:
                event, payload = self.events.get_nowait()
                if event == "selection":
                    self.picking = False
                    variable, selected, error = payload
                    if selected is not None:
                        variable.set(str(selected))
                    if error:
                        self.messagebox.showerror("TriAevum Forge", error, parent=self.root)
                    self._refresh_actions()
                elif event == "stage":
                    _stage, message = payload
                    self.status.set(str(message))
                elif event == "error":
                    self.busy = False
                    self.progress.stop()
                    self.status.set("Title preparation failed.")
                    self.messagebox.showerror(
                        "TriAevum Forge", str(payload), parent=self.root
                    )
                    self._refresh_actions()
                elif event == "complete":
                    self.busy = False
                    self.progress.stop()
                    self._refresh_active_title()
                    self._refresh_actions()
                    self.messagebox.showinfo(
                        "TriAevum Forge",
                        "The title is ready. You can launch TriAevum now.",
                        parent=self.root,
                    )
        except queue.Empty:
            pass
        self.root.after(100, self._poll_events)

    def _close(self) -> None:
        if self.busy and not self.messagebox.askyesno(
            "TriAevum Forge",
            "Title preparation is still running. Close Forge?",
            parent=self.root,
        ):
            return
        self.closing.set()
        if self.picker_thread is not None:
            self.picker_thread.join(timeout=15)
        process = self.worker_process
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        self.root.destroy()


def present_window(root: Any) -> None:
    root.deiconify()
    root.lift()
    root.attributes("-topmost", True)
    root.after(300, lambda: root.attributes("-topmost", False))
    root.focus_force()


def main(*, startup_error: str | None = None) -> int:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError as exc:
        print(f"TriAevum Forge GUI is unavailable: {exc}")
        return 1

    try:
        root = tk.Tk()
        ForgeWindow(root, tk, ttk, filedialog, messagebox)
    except (OSError, ValueError, forge.ForgeError) as exc:
        try:
            messagebox.showerror("TriAevum Forge", str(exc))
        except Exception:
            print(f"TriAevum Forge GUI failed: {exc}")
        return 1

    _hide_explorer_console()
    if startup_error:
        root.after(0, lambda: messagebox.showerror("TriAevum", startup_error, parent=root))
    present_window(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
