import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from common import atomic_write_json, sha256_file
from device_pipeline_preparation import (
    FORMAT, CACHE_FILENAME, _run, prepare_device_pipelines, renderer_cache_directory, adopt_existing_cache,
)


class DevicePreparationTests(unittest.TestCase):
    def setUp(self):
        self.environment = patch.dict(os.environ, {"TRIAEVUM_RENDERER_CACHE_DIR": ""})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.pack = self.root / "pack.o3ps"
        self.pack.write_bytes(b"fixture")
        self.cache = self.root / "cache"
        self.title = {"device_pipeline_preparation": {
            "format": FORMAT, "helper": self.artifact("helper"),
            "manifest": self.artifact("manifest.json"), "dependencies": [self.artifact("dependency")]}}

    def artifact(self, name):
        path = self.root / name
        path.write_bytes(b"fixture")
        return {"path": name, "bytes": 7, "sha256": sha256_file(path)}

    def prepare(self):
        return prepare_device_pipelines(root=self.root, data_root=self.root / "data",
            title=self.title, pack=self.pack, cache_directory=self.cache)

    def fake_run(self, command, root, stage, cancel, report):
        self.assertEqual(command[command.index("--pack") + 1], str(self.pack))
        self.cache.mkdir(exist_ok=True)
        cache = self.cache / CACHE_FILENAME
        cache.write_bytes(b"device cache")
        atomic_write_json(stage / "result.json", {
            "format": FORMAT, "device_pipeline_prewarm": "complete",
            "total": 500, "prepared": 500, "failed": 0, "game_booted": False,
            "cache_path": str(cache), "gpu": "test device"})
        return 0

    def test_off_is_inert(self):
        with patch("device_pipeline_preparation._run") as run:
            self.assertIsNone(prepare_device_pipelines(root=self.root, data_root=self.root, title={}, pack=None))
            run.assert_not_called()

    def test_device_is_checked_again_on_every_install(self):
        with patch("device_pipeline_preparation._run", side_effect=self.fake_run) as run:
            for _ in range(2):
                result = self.prepare()
                self.assertEqual(result["device_pipeline_prewarm"], "complete")
                self.assertFalse(result["game_coverage_proven"])
                self.assertEqual(result["pack_sha256"], sha256_file(self.pack))
            self.assertEqual(run.call_count, 2)

    def test_native_and_effect_manifests_share_one_device_job_and_cache(self):
        contract = self.title["device_pipeline_preparation"]
        native = contract.pop("manifest")
        contract["manifests"] = [native, self.artifact("effects.json"), native]
        with patch("device_pipeline_preparation._run", side_effect=self.fake_run) as run:
            result = self.prepare()
            self.assertEqual(result["device_pipeline_prewarm"], "complete")
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args.args[0].count("--manifest"), 2)
            self.assertEqual(len(result["manifest_sha256s"]), 2)

    def test_ambiguous_or_empty_manifests_do_not_run(self):
        contract = self.title["device_pipeline_preparation"]
        with patch("device_pipeline_preparation._run") as run:
            contract["manifests"] = [contract["manifest"]]
            with self.assertRaisesRegex(ValueError, "not both"): self.prepare()
            del contract["manifest"]
            contract["manifests"] = []
            with self.assertRaisesRegex(ValueError, "1 to 64"): self.prepare()
            run.assert_not_called()

    def test_missing_gpu_report_or_failed_job_is_not_success(self):
        for behavior in (lambda *_: 1, lambda *_: (_ for _ in ()).throw(TimeoutError("timeout"))):
            with patch("device_pipeline_preparation._run", side_effect=behavior):
                self.assertEqual(self.prepare()["device_pipeline_prewarm"], "failed")

    def test_partial_result_does_not_activate_a_success_receipt(self):
        def partial(command, root, stage, cancel, report):
            self.fake_run(command, root, stage, cancel, report)
            return 3
        with patch("device_pipeline_preparation._run", side_effect=partial):
            self.assertEqual(self.prepare()["device_pipeline_prewarm"], "failed")

    def test_unverified_artifact_never_executes(self):
        self.title["device_pipeline_preparation"]["helper"]["sha256"] = "0" * 64
        with patch("device_pipeline_preparation._run") as run:
            with self.assertRaises(ValueError): self.prepare()
            run.assert_not_called()

    def test_bad_adapter_or_missing_pack_never_executes(self):
        for value in (-1, 64, True, "1"):
            self.title["device_pipeline_preparation"]["adapter"] = value
            with self.assertRaises(ValueError): self.prepare()
        del self.title["device_pipeline_preparation"]["adapter"]
        self.pack.unlink()
        with self.assertRaises(ValueError): self.prepare()

    def test_sdl_paths_are_host_only_policy(self):
        with patch("device_pipeline_preparation.sys.platform", "linux"), \
                patch.dict(os.environ, {"XDG_DATA_HOME": str(self.root)}):
            self.assertEqual(renderer_cache_directory(), self.root / "oot3d_native_vulkan" / "shader_cache")
        with patch("device_pipeline_preparation.sys.platform", "win32"), \
                patch.dict(os.environ, {"APPDATA": str(self.root)}):
            self.assertEqual(renderer_cache_directory(), self.root / "oot3d_native_vulkan" / "shader_cache")

    def test_subprocess_timeout_is_reaped(self):
        with self.assertRaises(TimeoutError):
            _run([sys.executable, "-c", "import time; time.sleep(60)"],
                 self.root, self.root, lambda: False, lambda *_: None, timeout=0.1)

    def test_explicit_cache_directory_matches_renderer_override(self):
        with patch.dict(os.environ, {"TRIAEVUM_RENDERER_CACHE_DIR": str(self.cache)}):
            self.assertEqual(renderer_cache_directory(), self.cache)
        with patch.dict(os.environ, {"TRIAEVUM_RENDERER_CACHE_DIR": "relative/cache"}):
            with self.assertRaisesRegex(ValueError, "must be absolute"):
                renderer_cache_directory()

    def test_existing_cache_is_copied_without_overwrite_or_unknown_files(self):
        source = self.root / "previous-cache"
        (source / "spirv-v2").mkdir(parents=True)
        for name in (CACHE_FILENAME, "spirv-v2/ab_cd.spvc", "unknown.bin", "spirv-v2/invalid-name.spvc"):
            (source / name).write_bytes(b"fixture, validated only by renderer")
        self.assertEqual(adopt_existing_cache(self.cache, source=source), 2)
        self.assertFalse((self.cache / "unknown.bin").exists())
        (self.cache / CACHE_FILENAME).write_bytes(b"new learned cache")
        self.assertEqual(adopt_existing_cache(self.cache, source=source), 0)
        self.assertEqual((self.cache / CACHE_FILENAME).read_bytes(), b"new learned cache")
        self.assertTrue((source / "spirv-v2/ab_cd.spvc").is_file())

    def test_subprocess_progress_and_cooperative_cancel(self):
        messages = []
        program = ("import json,pathlib,time; "
                   "print(json.dumps({'event':'pipeline_progress','attempted':1,'total':2}),flush=True); "
                   "p=pathlib.Path('cancel'); "
                   "\nwhile not p.exists(): time.sleep(.01)\ntime.sleep(.1)")
        self.assertEqual(_run([sys.executable, "-c", program], self.root, self.root,
                              lambda: True, lambda *value: messages.append(value)), 0)
        self.assertTrue(messages)
        self.assertTrue((self.root / "cancel").is_file())

    @unittest.skipUnless(all(os.environ.get(key) for key in (
        "TRIAEVUM_TEST_NRI_PREPARE_HELPER", "TRIAEVUM_TEST_NRI_PIPELINE_MANIFEST", "TRIAEVUM_TEST_NRI_SHADER_PACK")),
        "optional hardware pipeline preparation qualification")
    def test_actual_device_job_reuse_corruption_and_cancel(self):
        def copy_artifact(variable, name):
            target = self.root / name
            shutil.copy2(os.environ[variable], target)
            return {"path": name, "bytes": target.stat().st_size, "sha256": sha256_file(target)}
        self.title["device_pipeline_preparation"] = {
            "format": FORMAT,
            "helper": copy_artifact("TRIAEVUM_TEST_NRI_PREPARE_HELPER", "prepare" + Path(os.environ["TRIAEVUM_TEST_NRI_PREPARE_HELPER"]).suffix),
            "manifest": copy_artifact("TRIAEVUM_TEST_NRI_PIPELINE_MANIFEST", "pipelines.json")}
        copy_artifact("TRIAEVUM_TEST_NRI_SHADER_PACK", "pack.o3ps")
        first, second = self.prepare(), self.prepare()
        for result in (first, second):
            self.assertEqual(result["device_pipeline_prewarm"], "complete", result)
            self.assertGreater(result["prepared"], 0)
            self.assertEqual(result["creation_attempts"], result["prepared"])
            self.assertEqual(result["created"], result["prepared"])
        self.assertFalse(first["cache_input_loaded"])
        self.assertEqual(first["initial_cache_bytes"], 0)
        self.assertTrue(second["cache_input_loaded"])
        self.assertGreater(second["initial_cache_bytes"], 0)
        (self.cache / CACHE_FILENAME).write_bytes(b"invalid cache")
        repaired = self.prepare()
        self.assertEqual(repaired["device_pipeline_prewarm"], "complete", repaired)
        self.assertFalse(repaired["cache_input_loaded"])
        cancelled = prepare_device_pipelines(root=self.root, data_root=self.root / "data", title=self.title,
            pack=self.pack, cache_directory=self.cache, cancel=lambda: True)
        self.assertEqual(cancelled["device_pipeline_prewarm"], "cancelled", cancelled)
        self.assertEqual(cancelled["prepared"], 0)
        print(f"Forge NRI: gpu={first['gpu']} pipelines={first['prepared']} "
              f"first={first['seconds']:.3f}s reuse={second['seconds']:.3f}s; corruption/cancel passed")


if __name__ == "__main__":
    unittest.main()
