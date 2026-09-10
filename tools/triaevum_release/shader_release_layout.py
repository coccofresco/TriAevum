"""Bind renderer-owned shader preparation tools to a portable release catalog.

This module never promotes private game shader inventories or device caches.
"""

import copy
from pathlib import Path

try:
    from .precompiled_title_layout import artifact
    from .release_platform import WINDOWS, for_target
    from .shader_preparation import RENDERER_CONTRACT
except ImportError:
    from precompiled_title_layout import artifact
    from release_platform import WINDOWS, for_target
    from shader_preparation import RENDERER_CONTRACT


def bind_renderer_compiler(catalog: dict, compiler: Path,
                           dependencies: list[Path]) -> tuple[dict, list[dict]]:
    platform = for_target(catalog["target"])
    suffix = ".exe" if platform == WINDOWS else ""
    destination = "forge/oot3d_native_pica_aot_compiler" + suffix
    records = []
    items = []
    names = set()
    for source, relative, role in [
        (compiler, destination, "shader_preparation_tool"),
        *((path, "forge/" + path.name, "forge_runtime_module") for path in dependencies),
    ]:
        if relative.casefold() in names:
            raise ValueError("Duplicate shader preparation artifact: " + relative)
        names.add(relative.casefold())
        records.append(artifact(source, relative))
        items.append({"source": str(source.resolve()), "path": relative, "role": role})
    result = copy.deepcopy(catalog)
    if not result.get("titles"):
        raise ValueError("Renderer preparation requires an installation catalog")
    for title in result["titles"]:
        title["renderer_shader_preparation"] = {
            "format": RENDERER_CONTRACT, "compiler": records[0],
            "dependencies": records[1:],
        }
    return result, items
