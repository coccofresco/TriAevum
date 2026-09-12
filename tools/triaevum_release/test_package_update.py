import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools.triaevum_release import package_update as update
from tools.triaevum_release.common import atomic_write_json, sha256_file
from tools.triaevum_release.release_platform import host_platform


class PackageUpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.platform = host_platform()
        self.exe = self.root / self.platform.runtime
        self.exe.write_bytes(b'new runtime')
        self.title = self.root / 'data/titles/test'
        self.title.mkdir(parents=True)
        self.profile = self.root / 'TriAevum.launch.json'
        self.plugin = self.root / 'private-plugins/old' / self.platform.title_module
        self.plugin.parent.mkdir(parents=True)
        self.plugin.write_bytes(b'old plugin')
        arguments = ['--title-plugin', str(self.plugin)]
        for option, path in (
            ('--a32-process-manifest', self.title / 'process-manifest.json'),
            ('--config', self.root / 'data/config/TriAevum.json'),
            ('--topscreen-config', self.root / 'data/config/topscreen_ui.json'),
            ('--save-data', self.root / 'data/savedata'),
        ):
            if option != '--save-data':
                atomic_write_json(path, {})
            arguments += [option, str(path)]
        atomic_write_json(self.profile, {'format': 'oot3d_native_game_launch_profile_v1',
                                        'arguments': arguments})
        self.runtime = dict(status='ready', target=self.platform.target,
            runtime_sha256='old', plugin=str(self.plugin), plugin_sha256=sha256_file(self.plugin),
            launch_profile=str(self.profile), launch_profile_sha256=sha256_file(self.profile))
        self.catalog = dict(format='triaevum_precompiled_titles_v1',
            install_model='precompiled_title_rom_import_v1', target=self.platform.target,
            runtime=dict(path=self.platform.runtime, sha256=sha256_file(self.exe)),
            native_module=dict(path=self.platform.native_module),
            titles=[dict(recipe='test', plugin=dict(sha256='new plugin'))])
        atomic_write_json(self.root / update.CATALOG, self.catalog)
        self.args = dict(executable=self.exe, title=self.title, data_root=self.root / 'data',
                         recipe_id='test', recipes=self.root / 'recipes/oot3d.json')
        self.prepared = SimpleNamespace(state={'runtime': self.runtime})
        self.enter = self.enterContext
        self.enter(patch('tools.triaevum_release.forge.load_prepared_content', return_value=self.prepared))
        self.enter(patch('tools.triaevum_release.forge.load_recipe', return_value={'id': 'test'}))
        self.select = self.enter(patch.object(update, 'select_title', return_value=self.catalog['titles'][0]))
        self.install = self.enter(patch.object(update, 'install_precompiled_title'))

    def test_update_reuses_installer_without_mutating_previous_receipt(self):
        self.assertTrue(update.refresh_packaged_runtime(**self.args))
        self.select.assert_called_once()
        self.install.assert_called_once()
        self.assertEqual(self.runtime['runtime_sha256'], 'old')
        self.assertEqual(self.install.call_args.kwargs['launch_profile'], self.profile)
        # Publication must run outside the inspection lock.
        def publish(*args, **kwargs):
            with update.installation_lock(self.root):
                pass
        self.install.side_effect = publish
        self.assertTrue(update.refresh_packaged_runtime(**self.args))

    def test_current_installation_does_not_reinstall(self):
        self.runtime.update(runtime_sha256=self.catalog['runtime']['sha256'],
                            plugin_sha256='new plugin')
        self.assertFalse(update.refresh_packaged_runtime(**self.args))
        self.install.assert_not_called()

    def test_old_plugin_and_profile_corruption_are_not_laundered(self):
        self.plugin.write_bytes(b'damaged')
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            update.refresh_packaged_runtime(**self.args)
        self.plugin.write_bytes(b'old plugin')
        self.profile.write_text('{}')
        with self.assertRaisesRegex(ValueError, 'profile changed'):
            update.refresh_packaged_runtime(**self.args)
        self.install.assert_not_called()

    def test_corrupt_package_and_interrupted_activation_fail_closed(self):
        self.select.side_effect = ValueError('catalog integrity')
        with self.assertRaisesRegex(ValueError, 'catalog integrity'):
            update.refresh_packaged_runtime(**self.args)
        self.select.side_effect = None
        atomic_write_json(update.journal_path(self.root), {})
        with self.assertRaisesRegex(ValueError, 'interrupted'):
            update.refresh_packaged_runtime(**self.args)
        self.install.assert_not_called()

    def test_installer_failure_propagates_without_launching(self):
        self.install.side_effect = ValueError('ABI preflight failed')
        with self.assertRaisesRegex(ValueError, 'ABI preflight'):
            update.refresh_packaged_runtime(**self.args)


if __name__ == '__main__':
    unittest.main()
