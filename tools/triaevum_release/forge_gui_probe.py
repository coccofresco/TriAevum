"""Bounded, real-widget smoke test for source and frozen Forge builds."""

import argparse
import json
from pathlib import Path
import sys
import time


def main(arguments):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rom", type=Path, help="Actually install this private ROM through the GUI worker")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args(arguments)
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    from .forge_gui import ForgeWindow, runtime_path, present_window

    notifications = []

    class ProbeDialogs:
        # Record terminal dialogs without blocking unattended qualification.
        @staticmethod
        def showerror(_title, message, **_kwargs):
            notifications.append({"kind": "error", "message": str(message)})

        @staticmethod
        def showinfo(_title, message, **_kwargs):
            notifications.append({"kind": "info", "message": str(message)})

        @staticmethod
        def askyesno(*_args, **_kwargs):
            return True

    root = tk.Tk()
    window = ForgeWindow(root, tk, ttk, filedialog, ProbeDialogs if args.rom else messagebox)
    present_window(root)
    result = {}
    started = time.monotonic()

    def verify():
        try:
            root.update_idletasks()
            widgets = []
            pending = list(root.winfo_children())
            while pending:
                widget = pending.pop()
                pending.extend(widget.winfo_children())
                if widget.winfo_class() not in ("TButton", "TEntry", "TLabel", "TProgressbar"):
                    continue
                bounds = [widget.winfo_rootx() - root.winfo_rootx(),
                          widget.winfo_rooty() - root.winfo_rooty(),
                          widget.winfo_width(), widget.winfo_height()]
                widgets.append({"class": widget.winfo_class(), "bounds": bounds,
                                "mapped": bool(widget.winfo_ismapped())})
            clipped = [item for item in widgets if item["bounds"][0] < 0 or item["bounds"][1] < 0 or
                       item["bounds"][0] + item["bounds"][2] > root.winfo_width() or
                       item["bounds"][1] + item["bounds"][3] > root.winfo_height()]
            unmapped = [item for item in widgets if not item["mapped"]]
            window.rom.set("probe.cci")
            enabled = str(window.install_button.cget("state")) == "normal"
            window.rom.set("")
            disabled = str(window.install_button.cget("state")) == "disabled"
            result.update(status="passed" if widgets and not clipped and not unmapped and enabled and disabled else "failed",
                          frozen=bool(getattr(sys, "frozen", False)), platform=sys.platform,
                          size=[root.winfo_width(), root.winfo_height()],
                          window_state=root.state(),
                          runtime=str(runtime_path()), widgets=widgets, clipped_widgets=clipped,
                          unmapped_widgets=unmapped,
                          prepare_button_tracks_input=enabled and disabled)
            if args.rom:
                installed = (window.active_title is not None and not window.busy and
                             any(item["kind"] == "info" for item in notifications) and
                             not any(item["kind"] == "error" for item in notifications) and
                             str(window.launch_button.cget("state")) == "normal")
                result.update(installation_ready=installed, notifications=notifications,
                              install_wall_seconds=time.monotonic() - started)
                if not installed:
                    result["status"] = "failed"
        except Exception as error:
            result.update(status="failed", error=str(error))
        finally:
            window._close()

    def await_installation():
        if time.monotonic() - started >= args.timeout:
            notifications.append({"kind": "error", "message": "GUI installation timed out"})
            verify()
        elif notifications and not window.busy:
            # A desktop window manager may have minimized this unattended
            # window while its worker ran. Inspect a mapped window, not a tray.
            present_window(root)
            root.after(500, verify)
        else:
            root.after(100, await_installation)

    def install():
        window.rom.set(str(args.rom.resolve()))
        window.install_button.invoke()
        await_installation()

    root.after(1000, install if args.rom else verify)
    root.mainloop()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0 if result.get("status") == "passed" else 1
