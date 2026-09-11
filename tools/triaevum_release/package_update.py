"""Rebind prepared user data after a verified desktop package update.

No compiler fallback, ROM reimport, or relaxed launch validation. The existing
installer owns ABI preflight, immutable plugin generations and publication.
"""

from pathlib import Path

from .activation_transaction import installation_lock, journal_path
from .host_layout import for_package
from .installed_runtime import validate_installed_runtime
from .precompiled_titles import CATALOG, load_catalog, select_title, install_precompiled_title
from .release_platform import host_platform


def refresh_packaged_runtime(*, executable: Path, title: Path, data_root: Path,
                            recipe_id: str, recipes: Path) -> bool:
    from . import forge

    package = executable.parent
    if not (package / CATALOG).is_file():
        return False  # Developer installations have no publisher catalog.
    layout = for_package(package)
    with installation_lock(layout.activation):
        if journal_path(layout.activation).exists():
            raise ValueError("An activation was interrupted; run Forge before updating")
        prepared = forge.load_prepared_content(title, required_inputs=())
        runtime = prepared.state.get("runtime")
        if not isinstance(runtime, dict) or runtime.get("status") != "ready":
            return False
        catalog = load_catalog(package)
        matches = [item for item in catalog["titles"] if item["recipe"] == recipe_id]
        if len(matches) != 1:
            raise ValueError("The updated package does not support the installed title")
        candidate = matches[0]
        if (runtime.get("runtime_sha256") == catalog["runtime"]["sha256"] and
                runtime.get("plugin_sha256") == candidate["plugin"]["sha256"]):
            return False
        recipe = forge.load_recipe(recipes, recipe_id)
        select_title(package, recipe, catalog=catalog)
        # The old executable was replaced by the package manager. Validate every
        # other old binding, and the new executable against its catalog, before
        # allowing the installer to publish a replacement tuple.
        previous = dict(runtime, runtime_sha256=catalog["runtime"]["sha256"])
        profile = validate_installed_runtime(executable, title, data_root, previous)

    # The installer acquires the publication lock itself and revalidates prepared
    # inputs. Never nest its non-reentrant transaction under the inspection lock.
    install_precompiled_title(title, root=package, recipe=recipe, data_root=data_root,
        runtime_plugin=layout.activation / host_platform().title_module,
        launch_profile=profile, active_title_state=data_root / "active-title.json")
    return True
