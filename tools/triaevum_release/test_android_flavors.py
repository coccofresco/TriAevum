"""Static contract of the Android release flavors (no Gradle run: builds need a disk grant)."""

from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

from android_release_policy import TITLES, android_facts

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "ports/android/app"
FLAVORS = {"oot3d": "eot", "mm3d": "mc"}
DENSITIES = ("mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi")
SIGNING = ("TRIAEVUM_ANDROID_KEYSTORE", "TRIAEVUM_ANDROID_KEYSTORE_PASS", "TRIAEVUM_ANDROID_KEY_ALIAS",
           "TRIAEVUM_ANDROID_KEY_PASS")


def locales(source_set: str) -> set[str]:
    return {path.parent.name for path in (APP / "src" / source_set / "res").glob("values*/strings.xml")}


def strings(path: Path) -> dict[str, str]:
    return {item.get("name"): item.text or "" for item in ET.parse(path).getroot().iter("string")}


def flavor_errors(app: Path = APP) -> list[str]:
    errors = []
    main_locales = {path.parent.name for path in (app / "src/main/res").glob("values*/strings.xml")}
    for title in TITLES:
        facts = android_facts(title)
        flavor = facts["flavor"]
        if flavor != FLAVORS[title]:
            errors.append(f"{title}: flavor {flavor}")
        flavor_locales = {path.parent.name for path in (app / "src" / flavor / "res").glob("values*/strings.xml")}
        for missing in sorted(main_locales - flavor_locales):
            errors.append(f"{flavor}: no {missing}/strings.xml (the main locale would show the other product)")
        for path in sorted((app / "src" / flavor / "res").glob("values*/strings.xml")):
            values = strings(path)
            if values.get("app_name") != facts["label"]:
                errors.append(f"{flavor}/{path.parent.name}: app_name {values.get('app_name')!r}")
            if facts["label"] not in values.get("forge_title", ""):
                errors.append(f"{flavor}/{path.parent.name}: forge_title lacks the product name")
    for path in sorted((app / "src/main/res").glob("values*/strings.xml")):
        if not strings(path).get("forge_no_game_assets"):
            errors.append(f"main/{path.parent.name}: no forge_no_game_assets")
    for density in DENSITIES:
        for name in ("ic_launcher.png", "ic_launcher_round.png", "ic_launcher_foreground.png",
                     "ic_launcher_background.png"):
            path = app / "src/mc/res" / f"mipmap-{density}" / name
            if not path.is_file():
                errors.append(f"mc: missing {path.relative_to(app).as_posix()}")
            elif name != "ic_launcher_background.png" and path.read_bytes() == (app / "src/main/res" / f"mipmap-{density}" / name).read_bytes():
                errors.append(f"mc: {name} ({density}) is the Echoes of Time icon")
    gradle = (app / "build.gradle.kts").read_text(encoding="utf-8")
    for variable in SIGNING:
        if f'"{variable}"' not in gradle:
            errors.append(f"build.gradle.kts does not read {variable}")
    if "System.getenv" not in gradle or "throw GradleException(\"Release signing needs" not in gradle:
        errors.append("build.gradle.kts release signing does not fail closed")
    if re.search(r"(storePassword|keyPassword)\s*=\s*\"", gradle) or re.search(r"storeFile\s*=\s*file\(\"", gradle):
        errors.append("build.gradle.kts holds a literal signing secret or keystore path")
    if "isDebuggable = false" not in gradle or "withFlavor(\"title\" to \"dev\").withBuildType(\"release\")" not in gradle:
        errors.append("build.gradle.kts release type is debuggable or ships the dev identity")
    if "titles/$title/android.json" not in gradle:
        errors.append("build.gradle.kts does not read the title facts")
    # A5: a signed release demands the Forge package signature on the device.
    if 'buildConfigField("boolean", "VERIFY_FORGE_PACKAGE", (canSign && !unsignedCandidate).toString())' not in gradle:
        errors.append("build.gradle.kts: a signed release does not verify the Forge package")
    package = (app / "src/main/java/org/triaevum/android/forge/ForgePackage.kt")
    if package.is_file():
        text = package.read_text(encoding="utf-8")
        copy, check = text.find("copyTree(context.assets, ASSET_ROOT, staging)"), text.find(
            "if (BuildConfig.VERIFY_FORGE_PACKAGE) requireSigned(staging)")
        if check < 0 or copy < 0 or check < copy or check > text.find("staging.renameTo(target)") \
                or "ForgeNative.verifyPackage" not in text or '!= "signed"' not in text:
            errors.append("ForgePackage.prepare publishes the package without the signature check")
    return errors


class FlavorContractTests(unittest.TestCase):
    def test_flavors(self):
        self.assertEqual(flavor_errors(), [])

    def test_negative_forge_package_published_unverified(self):
        import shutil
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / "app"
            shutil.copytree(APP / "src", app / "src")
            shutil.copy2(APP / "build.gradle.kts", app / "build.gradle.kts")
            kt = app / "src/main/java/org/triaevum/android/forge/ForgePackage.kt"
            kt.write_text(kt.read_text(encoding="utf-8").replace(
                "        if (BuildConfig.VERIFY_FORGE_PACKAGE) requireSigned(staging)\n", ""), encoding="utf-8")
            self.assertIn("ForgePackage.prepare publishes the package without the signature check", flavor_errors(app))

    def test_negative_missing_locale_and_literal_secret(self):
        import shutil
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / "app"
            shutil.copytree(APP / "src", app / "src")
            shutil.copy2(APP / "build.gradle.kts", app / "build.gradle.kts")
            (app / "src/mc/res/values-it/strings.xml").unlink()
            gradle = app / "build.gradle.kts"
            gradle.write_text(gradle.read_text(encoding="utf-8").replace(
                'storePassword = signingEnvironment.getValue("TRIAEVUM_ANDROID_KEYSTORE_PASS")',
                'storePassword = "hunter2"'), encoding="utf-8")
            errors = flavor_errors(app)
            self.assertTrue(any("mc: no values-it" in error for error in errors), errors)
            self.assertTrue(any("literal signing secret" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
