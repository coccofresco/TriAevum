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
import webbrowser
from dataclasses import dataclass
import time
from pathlib import Path
from typing import Any, Callable

try:
    from . import ctr_rom, forge, extracted_inputs, game_language
    from .bundle_paths import installation_path, installation_root
    from .common import load_json_object
    from .installed_runtime import validate_installed_runtime
    from .activation_transaction import installation_lock, journal_path
    from .installation_context import resolve_reference
    from .precompiled_titles import load_catalog, select_title, install_precompiled_title
    from .input_adapters import import_contract, adapt_extracted_inputs
    from .release_platform import host_platform, installed_runtime_path, place_installed_runtime
    from .native_process import popen_native
    from .host_layout import for_package
    from . import portal_picker
    from . import romfs_mods
    from . import ui_texture_pack
except ImportError:
    import ctr_rom
    import forge
    import extracted_inputs
    import game_language
    from bundle_paths import installation_path, installation_root
    from common import load_json_object
    from installed_runtime import validate_installed_runtime
    from activation_transaction import installation_lock, journal_path
    from installation_context import resolve_reference
    from precompiled_titles import load_catalog, select_title, install_precompiled_title
    from input_adapters import import_contract, adapt_extracted_inputs
    from release_platform import host_platform, installed_runtime_path, place_installed_runtime
    from native_process import popen_native
    from host_layout import for_package
    import portal_picker
    import romfs_mods
    import ui_texture_pack


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
        forge.check_title_path_length(
            output_root, [option.recipe_id for option in load_recipe_options()])
        root = installation_root()
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
    return installed_runtime_path(installation_root(), host_platform()).resolve()


def launch_runtime(data_root: Path | None = None) -> subprocess.Popen[bytes]:
    try:
        from tools.triaevum_release.package_update import refresh_packaged_runtime
        layout = for_package(installation_root())
        with installation_lock(layout.activation):
            if journal_path(layout.activation).exists():
                raise forge.ForgeError("An activation was interrupted; run Forge again before playing")
            # A package unpacked over the installation brings a new runtime.
            place_installed_runtime(installation_root(), host_platform())
        if (installation_root() / "recipes/precompiled-titles.json").is_file():
            private_root = (data_root or default_gui_data_root()).expanduser().resolve()
            active = load_active_title(private_root / "active-title.json")
            if active is not None:
                refresh_packaged_runtime(executable=runtime_path(), title=active.directory,
                    data_root=private_root, recipe_id=active.recipe_id, recipes=forge.DEFAULT_RECIPES)
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
        cwd=for_package(installation_root()).activation,
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

        self.root.title("TriAevum Forge — Echoes of Time")
        self.root.geometry("760x390")
        self.root.minsize(760, 390)
        self.root.protocol("WM_DELETE_WINDOW", self._close)

        self.data_root = default_gui_data_root()
        self.rom = tk.StringVar()
        self.language = tk.StringVar()
        self.language_options = []
        self.status = tk.StringVar(value="Checking the local installation...")
        self.hd_status = tk.StringVar()
        self.hd_selection = tk.StringVar()
        self.hd_folder = tk.StringVar()
        # On by default: a title prepared without the archive stored gets the
        # HD UI texture pack downloaded first, as part of "Prepare and install".
        self.hd_auto_download = tk.BooleanVar(value=True)
        self.hd_pack = ui_texture_pack.VANILLA_UI_HUD
        self.hd_contract = ui_texture_pack.pack_contract(forge.DEFAULT_RECIPES, self.hd_pack)
        # Optional RomFS mods: off by default. This section is the setting's
        # only control; "Prepare and install" applies it.
        self.mod = romfs_mods.VANILLA_PLUS_LINK
        self.mod_contract = romfs_mods.archive_contract(forge.DEFAULT_RECIPES, self.mod)
        self.mod_status = tk.StringVar()
        self.mod_selection = tk.StringVar()
        self.mod_folder = tk.StringVar()
        self.mod_settings_error = None
        try:
            enabled = self.mod.mod_id in romfs_mods.enabled_mods(self.data_root)
        except (OSError, ValueError) as exc:
            enabled = False
            self.mod_settings_error = str(exc)
        self.mod_enabled = tk.BooleanVar(value=enabled)
        self.install_thread = None

        self._build_layout()
        self.rom.trace_add("write", lambda *_args: self._refresh_actions())
        self.hd_selection.trace_add("write", lambda *_args: self._import_hd())
        self.hd_folder.trace_add("write", lambda *_args: self._import_hd_folder())
        self.hd_auto_download.trace_add("write", lambda *_args: self._refresh_hd())
        self.mod_selection.trace_add("write", lambda *_args: self._import_mod_archive())
        self.mod_folder.trace_add("write", lambda *_args: self._import_mod_folder())
        self.mod_enabled.trace_add("write", lambda *_args: self._toggle_mod())
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
                "containing decompressed code.bin, exheader.bin and romfs.bin."
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
        self._build_hd_section(outer, 6)
        self._build_mods_section(outer, 7)
        separator = self.ttk.Separator(outer)
        separator.grid(row=8, column=0, columnspan=3, sticky="ew", pady=(18, 14))

        self.status_label = self.ttk.Label(
            outer, textvariable=self.status, wraplength=700, justify="left"
        )
        self.status_label.grid(row=9, column=0, columnspan=3, sticky="ew")
        self.progress = self.ttk.Progressbar(outer, mode="indeterminate")
        self.progress.grid(row=10, column=0, columnspan=3, sticky="ew", pady=(10, 18))

        actions = self.ttk.Frame(outer)
        actions.grid(row=11, column=0, columnspan=3, sticky="ew")
        actions.columnconfigure(0, weight=1)
        self.install_button = self.ttk.Button(
            actions, text="Prepare and install", command=self._install
        )
        self.install_button.grid(row=0, column=1, padx=(8, 0))
        self.launch_button = self.ttk.Button(
            actions, text="Launch game", command=self._launch
        )
        self.launch_button.grid(row=0, column=2, padx=(8, 0))

    def _build_hd_section(self, parent: Any, row: int) -> None:
        # The HD graphics are not in the package: make obtaining them obvious.
        style = self.ttk.Style(self.root)
        style.configure("HD.TLabelframe.Label", font=("Segoe UI", 11, "bold"))
        style.configure("HD.TButton", font=("Segoe UI", 10, "bold"), padding=(12, 4))
        style.configure("Link.TLabel", foreground="#1a5fb4")
        pack = self.hd_pack
        section = self.ttk.Labelframe(
            parent, text="HD UI textures (recommended)", style="HD.TLabelframe", padding=12)
        section.grid(row=row, column=0, columnspan=3, sticky="ew", pady=(16, 0))
        section.columnconfigure(3, weight=1)
        credits = ", ".join(credit.name for credit in pack.credits if credit.group == "Key Authors")
        self.ttk.Label(
            section,
            text=(
                f"{pack.title} {pack.version} by {pack.author} (credits: {credits}): HD textures for "
                "the HUD, the menus and TopScreen, in the game language you choose here or in game. "
                f"Licensed {pack.license_name}: TriAevum does not distribute it. Forge downloads the "
                "author's original archive from GameBanana, verifies it and keeps it in your private "
                "data folder. If the download does not work, download the zip from the mod page and "
                "select it here. Thanks to RLG (M-1) for SingleScreen, the mod TriAevum's TopScreen is "
                "based on; its HD font is included with TriAevum."
            ),
            wraplength=680,
            justify="left",
        ).grid(row=0, column=0, columnspan=4, sticky="ew")
        self.hd_download_button = self.ttk.Button(
            section, text="Download HD textures", style="HD.TButton", command=self._download_hd)
        self.hd_download_button.grid(row=1, column=0, sticky="w", pady=(10, 0))
        self.hd_select_button = self.ttk.Button(
            section, text="Select .zip...",
            command=lambda: self._browse(
                self.hd_selection,
                f"Select {self.hd_contract.name if self.hd_contract else 'the texture pack zip'}",
                (("Texture pack archive", "*.zip"),)))
        self.hd_select_button.grid(row=1, column=1, sticky="w", padx=(8, 0), pady=(10, 0))
        self.hd_folder_button = self.ttk.Button(
            section, text="Select extracted folder...", command=self._select_hd_folder)
        self.hd_folder_button.grid(row=1, column=2, sticky="w", padx=(8, 0), pady=(10, 0))
        links = self.ttk.Frame(section)
        links.grid(row=1, column=3, sticky="e", pady=(10, 0))
        for column, (text, url) in enumerate((("Mod page", pack.page), ("License", pack.license_url))):
            link = self.ttk.Label(links, text=text, style="Link.TLabel", cursor="hand2")
            link.grid(row=0, column=column, padx=(8, 0))
            link.bind("<Button-1>", lambda _event, target=url: webbrowser.open(target))
        self.hd_auto_check = self.ttk.Checkbutton(
            section, variable=self.hd_auto_download,
            text="Download the HD textures with \"Prepare and install\" when they are not stored yet")
        self.hd_auto_check.grid(row=2, column=0, columnspan=4, sticky="w", pady=(8, 0))
        self.ttk.Label(section, textvariable=self.hd_status, wraplength=680, justify="left").grid(
            row=3, column=0, columnspan=4, sticky="ew", pady=(8, 0))
        self._refresh_hd()

    def _hd_state(self) -> str:
        return ui_texture_pack.pack_status(installation_root(), self.data_root, self.hd_contract, self.hd_pack)

    def _refresh_hd(self) -> None:
        if self.hd_contract is None:
            self.hd_status.set("Not available for the supported game revisions of this package.")
            return
        state = self._hd_state()
        if state == "ready":
            self.hd_status.set("HD textures ready. They are applied when you prepare and install the title.")
        elif state == "archive":
            self.hd_status.set("HD texture archive stored. It is prepared and applied when you prepare "
                               "and install the title.")
        elif self.hd_auto_download.get():
            self.hd_status.set(f"Not stored yet: the HD textures ({self.hd_contract.bytes // 1_000_000} MB) "
                               "are downloaded when you prepare and install the title. Without them the "
                               "game uses the native textures.")
        else:
            self.hd_status.set("Not installed: the game uses the native textures.")

    def _install_hd_download_needed(self) -> bool:
        """Whether "Prepare and install" downloads the HD textures first."""
        return (bool(self.hd_auto_download.get()) and self.hd_contract is not None
                and self._hd_state() == "missing")

    def _download_hd_task(self, report: Callable[[str], None]) -> None:
        romfs_mods.download_archive(
            self.data_root, self.hd_pack, self.hd_contract, report=report,
            progress=lambda done, total: report(f"Downloading the HD textures... {done * 100 // total}%"))

    def _download_hd_for_install(self) -> None:
        """Worker-thread download before the title is prepared; a failure keeps
        the native textures and reports why, it never stops the installation."""
        self.events.put(("hd_stage", "Downloading the HD textures..."))
        try:
            self._download_hd_task(lambda text: self.events.put(("hd_stage", text)))
        except Exception as exc:
            self.events.put(("hd_stage", (
                f"HD texture download failed: {exc} The title is prepared with the native textures. "
                "Download the zip from the mod page and select it with \"Select .zip...\", then "
                "prepare and install again.")))
            return
        self.events.put(("hd_stage", "HD texture archive ready. It is applied by this installation."))

    def _download_hd(self) -> None:
        self._run_hd(self._download_hd_task, "Downloading the HD textures...")

    def _import_hd(self) -> None:
        selected = self.hd_selection.get().strip()
        if not selected or self.hd_contract is None:
            return
        self._run_hd(lambda _report: romfs_mods.import_archive(
            Path(selected), self.data_root, self.hd_pack, self.hd_contract),
            "Verifying the selected archive...")

    def _select_hd_folder(self) -> None:
        if self.busy or self.picking:
            return
        title = f"Select the extracted {self.hd_pack.title} folder"
        if for_package(installation_root()).use_file_portal:
            self._portal_browse(self.hd_folder, title, directory=True)
            return
        selected = self.filedialog.askdirectory(title=title, parent=self.root)
        if selected:
            self.hd_folder.set(selected)

    def _import_hd_folder(self) -> None:
        selected = self.hd_folder.get().strip()
        if selected:
            self._run_hd(lambda _report: ui_texture_pack.import_extracted(
                Path(selected), self.data_root, self.hd_pack), "Verifying the extracted textures...")

    def _run_hd(self, task: Callable[[Callable[[str], None]], Any], message: str) -> None:
        if self.busy or self.picking or self.hd_contract is None:
            return
        self.busy = True
        self.progress.start(12)
        self.hd_status.set(message)
        self._refresh_actions()

        def worker() -> None:
            try:
                task(lambda text: self.events.put(("hd_stage", text)))
                self.events.put(("hd", None))
            except Exception as exc:
                self.events.put(("hd", str(exc)))
        threading.Thread(target=worker, name="TriAevumHdTextures", daemon=True).start()

    def _build_mods_section(self, parent: Any, row: int) -> None:
        # Third-party mods are never in the package: Forge obtains the author's
        # original archive on request and applies it to the local data only.
        mod = self.mod
        section = self.ttk.Labelframe(parent, text="Optional mods", style="HD.TLabelframe", padding=12)
        section.grid(row=row, column=0, columnspan=3, sticky="ew", pady=(12, 0))
        section.columnconfigure(3, weight=1)
        testers = ", ".join(credit.name for credit in mod.credits if credit.group == "Testing")
        tools = ", ".join(credit.name for credit in mod.credits if credit.group == "Tool Creation")
        self.ttk.Label(
            section,
            text=(f"{mod.title} {mod.version} by {mod.author}: higher-fidelity models for Link and "
                  f"his items. Testing: {testers}. Tools: {tools}. Licensed {mod.license_name}: "
                  "TriAevum does not distribute it. Forge downloads the author's original archive "
                  "from GameBanana, verifies it and applies it to your local game data only. If the "
                  "download does not work, download the archive from the mod page and select it here."),
            wraplength=680, justify="left",
        ).grid(row=0, column=0, columnspan=4, sticky="ew")
        self.mod_check = self.ttk.Checkbutton(
            section, variable=self.mod_enabled,
            text=f"Use {mod.title} (applied by \"Prepare and install\")")
        self.mod_check.grid(row=1, column=0, columnspan=4, sticky="w", pady=(8, 0))
        self.mod_download_button = self.ttk.Button(section, text="Download mod", command=self._download_mod)
        self.mod_download_button.grid(row=2, column=0, sticky="w", pady=(8, 0))
        self.mod_select_button = self.ttk.Button(
            section, text="Select .rar...",
            command=lambda: self._browse(self.mod_selection, f"Select {self.mod_contract.name if self.mod_contract else 'the archive'}",
                                         (("Mod archive", "*.rar"),)))
        self.mod_select_button.grid(row=2, column=1, sticky="w", padx=(8, 0), pady=(8, 0))
        self.mod_folder_button = self.ttk.Button(
            section, text="Select extracted folder...", command=self._select_mod_folder)
        self.mod_folder_button.grid(row=2, column=2, sticky="w", padx=(8, 0), pady=(8, 0))
        links = self.ttk.Frame(section)
        links.grid(row=2, column=3, sticky="e", pady=(8, 0))
        for column, (text, url) in enumerate((("Mod page", mod.page), ("License", mod.license_url))):
            link = self.ttk.Label(links, text=text, style="Link.TLabel", cursor="hand2")
            link.grid(row=0, column=column, padx=(8, 0))
            link.bind("<Button-1>", lambda _event, target=url: webbrowser.open(target))
        self.ttk.Label(section, textvariable=self.mod_status, wraplength=680, justify="left").grid(
            row=3, column=0, columnspan=4, sticky="ew", pady=(8, 0))
        self._refresh_mod()

    def _mod_state(self) -> str:
        return romfs_mods.mod_status(installation_root(), self.data_root, self.mod, self.mod_contract)

    def _refresh_mod(self) -> None:
        if self.mod_settings_error:
            self.mod_status.set(f"Cannot read the mod settings: {self.mod_settings_error}")
            return
        if self.mod_contract is None:
            self.mod_status.set("Not available for the supported game revisions of this package.")
            return
        state = self._mod_state()
        enabled = bool(self.mod_enabled.get())
        if state == "missing":
            self.mod_status.set("Not downloaded. It is downloaded when you prepare and install the title."
                                if enabled else "Off: the game uses its native models.")
        elif enabled:
            self.mod_status.set("On: applied when you prepare and install the title.")
        else:
            self.mod_status.set("Off: the game uses its native models. The downloaded archive is kept.")

    def _toggle_mod(self) -> None:
        enabled = bool(self.mod_enabled.get())
        try:
            romfs_mods.set_mod_enabled(self.data_root, self.mod.mod_id, enabled)
            self.mod_settings_error = None
        except (OSError, ValueError) as exc:
            self.messagebox.showerror("TriAevum Forge", f"Cannot save the mod setting: {exc}", parent=self.root)
            self.mod_settings_error = str(exc)
        self._refresh_mod()
        if self.active_title is not None and not self.mod_settings_error:
            self.mod_status.set(self.mod_status.get() + " Prepare and install the title again to apply "
                                "the change.")

    def _install_mod_download_needed(self) -> bool:
        return (bool(self.mod_enabled.get()) and self.mod_contract is not None
                and self._mod_state() == "missing")

    def _download_mod_task(self, report: Callable[[str], None]) -> str:
        name = f"{self.mod.title} {self.mod.version}"
        romfs_mods.download_archive(
            self.data_root, self.mod, self.mod_contract, report=report,
            progress=lambda done, total: report(f"Downloading {name}... {done * 100 // total}%"))
        return name

    def _download_mod_for_install(self) -> None:
        """Worker-thread download before the title is prepared; a failure keeps
        the native models and reports why, it never stops the installation."""
        try:
            self._download_mod_task(lambda text: self.events.put(("mod_stage", text)))
        except Exception as exc:
            self.events.put(("mod_stage", (
                f"Mod download failed: {exc} The title is prepared with the native models. Download "
                "the archive from the mod page and select it with \"Select .rar...\".")))

    def _download_mod(self) -> None:
        self._run_mod(self._download_mod_task, f"Downloading {self.mod.title}...")

    def _import_mod_archive(self) -> None:
        selected = self.mod_selection.get().strip()
        if not selected or self.mod_contract is None:
            return
        self._run_mod(lambda _report: romfs_mods.import_archive(
            Path(selected), self.data_root, self.mod, self.mod_contract),
            "Verifying the selected archive...")

    def _select_mod_folder(self) -> None:
        if self.busy or self.picking:
            return
        title = f"Select the extracted {self.mod.title} folder"
        if for_package(installation_root()).use_file_portal:
            self._portal_browse(self.mod_folder, title, directory=True)
            return
        selected = self.filedialog.askdirectory(title=title, parent=self.root)
        if selected:
            self.mod_folder.set(selected)

    def _import_mod_folder(self) -> None:
        selected = self.mod_folder.get().strip()
        if selected:
            self._run_mod(lambda _report: romfs_mods.import_extracted(
                Path(selected), self.data_root, self.mod), "Verifying the extracted files...")

    def _run_mod(self, task: Callable[[Callable[[str], None]], Any], message: str) -> None:
        if self.busy or self.picking or self.mod_contract is None:
            return
        self.busy = True
        self.progress.start(12)
        self.mod_status.set(message)
        self._refresh_actions()

        def worker() -> None:
            try:
                task(lambda text: self.events.put(("mod_stage", text)))
                self.events.put(("mod", None))
            except Exception as exc:
                self.events.put(("mod", str(exc)))
        threading.Thread(target=worker, name="TriAevumOptionalMod", daemon=True).start()

    def _browse_extracted(self) -> None:
        if self.busy or self.picking:
            return
        if for_package(installation_root()).use_file_portal:
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
        if for_package(installation_root()).use_file_portal:
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
        for button in (self.hd_download_button, self.hd_select_button, self.hd_folder_button,
                       self.hd_auto_check):
            button.configure(state="disabled" if self.busy or self.picking or self.hd_contract is None
                             else "normal")
        mod_available = self.mod_contract is not None and not self.mod_settings_error
        for button in (self.mod_check, self.mod_download_button, self.mod_select_button,
                       self.mod_folder_button):
            button.configure(state="disabled" if self.busy or self.picking or not mod_available
                             else "normal")

    def _install(self) -> None:
        if self.busy or self.picking:
            return
        request = self._request()
        download_hd = self._install_hd_download_needed()
        download_mod = self._install_mod_download_needed()
        self.busy = True
        self.progress.start(12)
        self.status.set("Downloading the HD textures before the title preparation..."
                        if download_hd else
                        "Downloading the enabled mod before the title preparation..."
                        if download_mod else "Starting title preparation...")
        self._refresh_actions()

        def worker() -> None:
            try:
                if download_hd and not self.closing.is_set():
                    self._download_hd_for_install()
                    self.events.put(("stage", ("preflight", "Starting title preparation...")))
                if download_mod and not self.closing.is_set():
                    self._download_mod_for_install()
                    self.events.put(("stage", ("preflight", "Starting title preparation...")))
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

        self.install_thread = threading.Thread(target=worker, name="TriAevumForge", daemon=True)
        self.install_thread.start()

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
                elif event == "hd_stage":
                    self.hd_status.set(str(payload))
                elif event == "mod_stage":
                    self.mod_status.set(str(payload))
                elif event == "mod":
                    self.busy = False
                    self.progress.stop()
                    self._refresh_mod()
                    self._refresh_actions()
                    if payload:
                        self.messagebox.showerror(
                            "TriAevum Forge",
                            f"{payload}\n\nDownload {self.mod_contract.name} from the mod page "
                            f"({self.mod.page}) and select it with \"Select .rar...\".",
                            parent=self.root)
                    elif self.active_title is not None and self.mod_enabled.get():
                        self.mod_status.set(f"{self.mod.title} is ready. Prepare and install the "
                                            "title again to apply it.")
                elif event == "hd":
                    self.busy = False
                    self.progress.stop()
                    self._refresh_hd()
                    self._refresh_actions()
                    if payload:
                        self.messagebox.showerror(
                            "TriAevum Forge",
                            f"{payload}\n\nDownload {self.hd_contract.name} from the mod page "
                            f"({self.hd_pack.page}) and select it with \"Select .zip...\".",
                            parent=self.root,
                        )
                    elif self.active_title is not None:
                        self.hd_status.set("HD textures ready. Prepare and install the title again "
                                           "to apply them.")
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
                    if not self.hd_status.get().startswith("HD texture download failed"):
                        self._refresh_hd()
                    if not self.mod_status.get().startswith("Mod download failed"):
                        self._refresh_mod()
                    self.messagebox.showinfo(
                        "TriAevum Forge",
                        "The title is ready. You can launch TriAevum — Echoes of Time now.",
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
