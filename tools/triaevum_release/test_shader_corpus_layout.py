import copy
import shutil
import tempfile
import unittest
from pathlib import Path

from common import atomic_write_json
from merge_shader_packs import encode
from precompiled_titles import checked_file
from release_platform import WINDOWS, LINUX
from shader_corpus_layout import bind_shader_corpus
from shader_preparation import prepare_shader_seed


class ShaderCorpusLayoutTests(unittest.TestCase):
    def fixture(self, root):
        pack = root / 'input.o3ps'
        pack.write_bytes(encode(3, {(2, 1, 2, 4): b'\x03\x02\x23\x07'}))
        manifest = root / 'input.json'
        atomic_write_json(manifest, dict(format='oot3d_pica_pipeline_manifest_v2',
            schema_version=2, descriptor_schema_version=3, pipeline_count=1, pipelines=[{}]))
        helper = root / 'helper'
        helper.write_bytes(b'test helper')
        return pack, manifest, helper

    def test_every_recipe_both_platforms_cold_warm_and_relocation(self):
        for platform in (WINDOWS, LINUX):
            with self.subTest(platform=platform.target), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                pack, manifest, helper = self.fixture(root)
                original = dict(target=platform.target, titles=[
                    dict(recipe=name, renderer_shader_preparation={'compiler': {'fixture': True}})
                    for name in ('eur', 'eur-catalogue', 'usa')])
                before = copy.deepcopy(original)
                catalog, items = bind_shader_corpus(original, pack, [manifest], helper)
                self.assertEqual(original, before)
                package = root / 'relocated Forge'
                for item in items:
                    destination = package / item['path']
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(item['source'], destination)
                for title in catalog['titles']:
                    self.assertIn('shader_preparation', title)
                    self.assertEqual(title['shader_preparation']['compiler'],
                                     title['renderer_shader_preparation']['compiler'])
                    checked_file(package, title['device_pipeline_preparation']['helper'])
                    events = []
                    cold = prepare_shader_seed(root=package, data_root=package / 'data',
                        title=title, report=lambda *event: events.append(event))
                    warm = prepare_shader_seed(root=package, data_root=package / 'data', title=title)
                    self.assertEqual(cold, warm)
                    self.assertEqual(cold.read_bytes(), pack.read_bytes())
                    self.assertTrue(events)
                    self.assertTrue(all(event[0] == 'shaders' for event in events))
                (package / catalog['titles'][0]['shader_preparation']['pack']['path']).write_bytes(b'bad')
                with self.assertRaises(ValueError):
                    prepare_shader_seed(root=package, data_root=package / 'data', title=catalog['titles'][0])

    def test_driver_cache_and_mismatching_schema_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pack, manifest, helper = self.fixture(root)
            catalog = dict(target=WINDOWS.target, titles=[dict(recipe='eur', renderer_shader_preparation={})])
            atomic_write_json(manifest, dict(format='oot3d_pica_pipeline_manifest_v2',
                schema_version=2, descriptor_schema_version=4, pipeline_count=1, pipelines=[{}]))
            with self.assertRaisesRegex(ValueError, 'manifest'):
                bind_shader_corpus(catalog, pack, [manifest], helper)
            pack.write_bytes(b'not a portable pack; GPU cache fixture')
            with self.assertRaises(ValueError):
                bind_shader_corpus(catalog, pack, [manifest], helper)


if __name__ == '__main__':
    unittest.main()
