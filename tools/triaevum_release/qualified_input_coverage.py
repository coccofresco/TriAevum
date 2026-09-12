"""Preserve reviewed ROM import contracts across platform/runtime rebuilds."""
import argparse
import copy
from pathlib import Path
import shutil

try:
    from .common import atomic_write_json, load_json_object
    from .data_compatibility import expected
    from .input_adapters import validate_adapter
    from .precompiled_titles import CATALOG, load_catalog, validate_title, checked_file
except ImportError:
    from common import atomic_write_json, load_json_object
    from data_compatibility import expected
    from input_adapters import validate_adapter
    from precompiled_titles import CATALOG, load_catalog, validate_title, checked_file

ROOT = Path(__file__).with_name("input_coverage")
DEFINITIONS = ROOT / "coverage.json"


def bind(catalog, recipes, *, root=ROOT):
    definitions = load_json_object(root / "coverage.json")
    catalog, recipes = copy.deepcopy(catalog), copy.deepcopy(recipes)
    artifacts = {}
    for binding in definitions["bindings"]:
        base_id = binding["base_recipe"]
        bases = [r for r in recipes["recipes"] if r["id"] == base_id]
        titles = [t for t in catalog["titles"] if t["recipe"] == base_id]
        if len(bases) != 1 or len(titles) != 1:
            raise ValueError("Qualified inputs require exactly one canonical recipe/title")
        base, title = bases[0], titles[0]
        if (base["inputs"] != binding["base_inputs"] or title["inputs"] != base["inputs"]
                or base.get("process") != binding["process"]
                or base.get("input_adapter") is not None):
            raise ValueError("Qualified inputs do not match the current canonical execution contract")
        identity = binding["input"]
        if identity["inputs"]["code"] != base["inputs"]["code"]:
            raise ValueError("Qualified input must preserve canonical code")
        existing = [r for r in recipes["recipes"] if r["id"] == identity["id"]]
        if len(existing) > 1:
            raise ValueError("Duplicate qualified recipe")
        recipe = copy.deepcopy(existing[0] if existing else base)
        if existing and (recipe["inputs"] != identity["inputs"] or recipe.get("process") != base.get("process")):
            raise ValueError("Existing input contract conflicts with qualification")
        for field in ("input_adapter", "data_compatibility"):
            if recipe.get(field) is not None and recipe[field] != identity.get(field):
                raise ValueError("Existing qualified contract conflicts")
        recipe.pop("precompiled_base_recipe", None)
        recipe.update(copy.deepcopy(identity))
        for phase in ("source", "execution"):
            if expected(recipe, phase) is None:
                raise ValueError("Qualified input requires both content families")
        validate_adapter(root, recipe)
        adapter = recipe.get("input_adapter")
        if adapter:
            record = adapter["code_copies"]
            source = checked_file(root, record)
            artifacts[record["path"]] = {
                "source": str(source), "path": record["path"], "role": "input_copy_adapter"}
        new_title = copy.deepcopy(title)
        new_title.update(recipe=recipe["id"], inputs=recipe["inputs"],
                         data_compatibility=recipe["data_compatibility"])
        new_title.pop("input_adapter", None)
        if adapter:
            new_title["input_adapter"] = copy.deepcopy(adapter)
        # Current platform binaries, provenance and shader preparation survive.
        recipes["recipes"] = [r for r in recipes["recipes"] if r["id"] != recipe["id"]] + [recipe]
        catalog["titles"] = [t for t in catalog["titles"] if t["recipe"] != recipe["id"]] + [new_title]
    return catalog, recipes, list(artifacts.values())


def require_coverage(catalog, recipes, *, root=ROOT):
    definitions = load_json_object(root / "coverage.json")
    present = {r["id"] for r in recipes["recipes"]}
    for binding in definitions["bindings"]:
        if binding["base_recipe"] not in present:
            continue
        identity = binding["input"]
        matches = [r for r in recipes["recipes"] if r["id"] == identity["id"]]
        titles = [t for t in catalog["titles"] if t["recipe"] == identity["id"]]
        if len(matches) != 1 or len(titles) != 1:
            raise ValueError("Release lost qualified input coverage: " + identity["id"])
        for field in ("inputs", "input_adapter", "data_compatibility"):
            if matches[0].get(field) != identity.get(field) or titles[0].get(field) != identity.get(field):
                raise ValueError("Release changed qualified input coverage: " + identity["id"])


def build_overlay(package, output):
    catalog = load_catalog(package)
    recipes = load_json_object(package / "recipes/oot3d.json")
    for recipe in recipes["recipes"]:
        validate_title(package, recipe, catalog=catalog)
    catalog, recipes, files = bind(catalog, recipes)
    require_coverage(catalog, recipes)
    output.mkdir(parents=True, exist_ok=False)
    for item in files:
        target = output / item["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(item["source"], target)
        item["source"] = str(target.resolve())
    for path, data, role in (
        ("recipes/oot3d.json", recipes, "revision_recipe"),
        (CATALOG, catalog, "precompiled_catalog"),
    ):
        atomic_write_json(output / path, data)
        files.append({"source": str((output / path).resolve()), "path": path, "role": role})
    atomic_write_json(output / "layout-overlay.json", {"files": files})
    return files


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build_overlay(args.package, args.output)
