"""Explicit, recoverable path migration; no title compilation or save conversion."""

from pathlib import Path

try:
    from . import platforms
    from .activation_transaction import activation_transaction
    from .common import atomic_write_json, load_json_object, sha256_file
    from .installation_context import InstallationContext, expand_profile_argument, resolve_reference
    from .installed_runtime import validate_installed_runtime
except ImportError:
    import platforms
    from activation_transaction import activation_transaction
    from common import atomic_write_json, load_json_object, sha256_file
    from installation_context import InstallationContext, expand_profile_argument, resolve_reference
    from installed_runtime import validate_installed_runtime


def migrate_installation(installation: Path, title: Path, data_root: Path) -> dict:
    try:
        from .forge import load_prepared_content
    except ImportError:
        from forge import load_prepared_content
    installation, title, data_root = (path.resolve() for path in (installation, title, data_root))
    if not title.is_relative_to(installation) or not data_root.is_relative_to(installation):
        raise ValueError("Portable migration requires title and data inside the installation")
    context = InstallationContext(installation)
    profile = installation / "TriAevum.launch.json"
    manifest_path = title / "process-manifest.json"
    state_path = title / "forge-state.json"
    index_path = title / "content.tap"
    active_path = data_root / "active-title.json"
    targets = [profile, manifest_path, state_path, index_path, active_path]
    if any(path.is_symlink() or not path.resolve().is_relative_to(installation) for path in targets):
        raise ValueError("Migration metadata must stay inside the installation without symbolic links")
    changed = []

    def write_if_changed(path, payload):
        if not path.exists() or load_json_object(path) != payload:
            atomic_write_json(path, payload)
            changed.append(context.reference(path, installation))

    with activation_transaction(installation, targets):
        prepared = load_prepared_content(title, required_inputs=("code", "exheader", "romfs"))
        runtime = prepared.state.get("runtime", {})
        selected_profile = validate_installed_runtime(installation / platforms.host().runtime, title, data_root, runtime)
        if selected_profile != profile:
            raise ValueError("Migration requires the installation's own default launch profile")
        runtime.setdefault("plugin", str(installation / platforms.host().plugin))
        manifest = load_json_object(manifest_path)
        fields = {"code": "code_bin_path", "exheader": "exheader_path", "romfs": "romfs_image_path"}
        external = []
        for group in ("inputs", "optional_inputs"):
            for kind, descriptor in prepared.index.get(group, {}).items():
                source = resolve_reference(descriptor["path"], title)
                descriptor["path"] = context.reference(source, title)
                descriptor["path_scope"] = context.scope(source)
                if descriptor["path_scope"] == "external":
                    external.append(kind)
                if group == "inputs" and kind in fields:
                    field = fields[kind]
                    if resolve_reference(manifest["source"][field], title) != source:
                        raise ValueError("Manifest input differs from the indexed input: " + kind)
                    manifest["source"][field] = descriptor["path"]
        manifest["source_path_scopes"] = {
            field: prepared.index["inputs"][kind]["path_scope"] for kind, field in fields.items()
        }
        write_if_changed(manifest_path, manifest)
        prepared.index["path_mode"] = "manifest_relative_v1"
        prepared.index["process_manifest"] = {
            "path": manifest_path.name, "bytes": manifest_path.stat().st_size,
            "sha256": sha256_file(manifest_path),
        }
        write_if_changed(index_path, prepared.index)
        payload = load_json_object(profile)
        arguments = payload["arguments"]
        scopes = {}
        path_options = {"--title-plugin", "--a32-process-manifest", "--resource-root",
                        "--config", "--topscreen-config", "--save-data", "--output"}
        for index, argument in enumerate(arguments[:-1]):
            if argument in path_options:
                path = Path(expand_profile_argument(arguments[index + 1], profile))
                if not path.is_absolute():
                    raise ValueError("Cannot migrate an ambiguous working-directory-relative argument")
                arguments[index + 1] = context.profile_argument(path, profile)
                scopes[argument] = context.scope(path)
        payload["path_scopes"] = scopes
        write_if_changed(profile, payload)
        for field in ("plugin", "launch_profile"):
            path = resolve_reference(runtime[field], title)
            runtime[field] = context.reference(path, title)
            runtime[field + "_scope"] = context.scope(path)
        runtime["launch_profile_sha256"] = sha256_file(profile)
        write_if_changed(state_path, prepared.state)
        if active_path.exists():
            active = load_json_object(active_path)
            if resolve_reference(active["directory"], active_path.parent) == title:
                active["directory"] = context.reference(title, active_path.parent)
                active["directory_scope"] = "relative"
                write_if_changed(active_path, active)
        load_prepared_content(title, required_inputs=("code", "exheader", "romfs"))
        validate_installed_runtime(installation / platforms.host().runtime, title, data_root, runtime)
    return {"status": "migrated" if changed else "unchanged", "changed_files": changed,
            "external_inputs": external, "saves_modified": False, "title_recompiled": False}
