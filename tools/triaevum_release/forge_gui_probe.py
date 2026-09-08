"""Bounded, real-widget smoke test for source and frozen Forge builds."""

import argparse
import json
from pathlib import Path
import sys


def main(arguments):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(arguments)
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    from .forge_gui import ForgeWindow, runtime_path

    root = tk.Tk()
    window = ForgeWindow(root, tk, ttk, filedialog, messagebox)
    result = {}

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
            clipped = [item for item in widgets if not item["mapped"] or
                       item["bounds"][0] < 0 or item["bounds"][1] < 0 or
                       item["bounds"][0] + item["bounds"][2] > root.winfo_width() or
                       item["bounds"][1] + item["bounds"][3] > root.winfo_height()]
            window.rom.set("probe.cci")
            enabled = str(window.install_button.cget("state")) == "normal"
            window.rom.set("")
            disabled = str(window.install_button.cget("state")) == "disabled"
            result.update(status="passed" if widgets and not clipped and enabled and disabled else "failed",
                          frozen=bool(getattr(sys, "frozen", False)), platform=sys.platform,
                          size=[root.winfo_width(), root.winfo_height()],
                          runtime=str(runtime_path()), widgets=widgets, clipped_widgets=clipped,
                          prepare_button_tracks_input=enabled and disabled)
        except Exception as error:
            result.update(status="failed", error=str(error))
        finally:
            root.destroy()

    root.after(1000, verify)
    root.mainloop()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0 if result.get("status") == "passed" else 1
