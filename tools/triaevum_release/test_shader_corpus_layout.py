import copy
import shutil
import tempfile
import unittest
from pathlib import Path

from common import atomic_write_json, sha256_file
from merge_shader_packs import encode
from precompiled_titles import checked_file
from release_platform import WINDOWS, LINUX
from shader_corpus_layout import bind_shader_corpus
from shader_preparation import prepare_shader_seed
from shader_corpus_qualification import INPUTS_FORMAT, inputs_digest, qualification_record


def write_qualification(path, pack, manifests, sources, generator=('generator.cpp',), runtime=('runtime.cpp',)):
    """A qualification record for pack and manifests, generated from sources."""
    def inputs(names):
        files = {name: sha256_file(sources / name) for name in names}
        return dict(format=INPUTS_FORMAT, targets=['fixture'], sha256=inputs_digest(files), files=files)
    atomic_write_json(path, qualification_record(generator=inputs(generator),
        policies=[inputs(runtime)], pack_sha256=sha256_file(pack),
        manifest_sha256=[sha256_file(item) for item in manifests], modules=1, requests=1))
    return path


class ShaderCorpusLayoutTests(unittest.TestCase):
    def fixture(self, root):
        pack = root / 'input.o3ps'
        pack.write_bytes(encode(3, {(2, 1, 2, 4): b'\x03\x02\x23\x07'}))
        manifest = root / 'input.json'
        atomic_write_json(manifest, dict(format='oot3d_pica_pipeline_manifest_v2',
            schema_version=2, descriptor_schema_version=3, pipeline_count=1, pipelines=[{}]))
        helper = root / 'helper'
        helper.write_bytes(b'test helper')
        self.sources = root / 'checkout'
        self.sources.mkdir()
        (self.sources / 'generator.cpp').write_text('generator', encoding='utf-8')
        (self.sources / 'runtime.cpp').write_text('runtime', encoding='utf-8')
        self.qualification = write_qualification(root / 'qualification.json', pack, [manifest], self.sources)
        return pack, manifest, helper

    def bind(self, catalog, pack, manifests, helper):
        return bind_shader_corpus(catalog, pack, manifests, helper, qualification=self.qualification,
                                  source_root=self.sources)

    def test_every_recipe_both_platforms_cold_warm_and_relocation(self):
        for platform in (WINDOWS, LINUX):
            with self.subTest(platform=platform.target), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                pack, manifest, helper = self.fixture(root)
                original = dict(target=platform.target, titles=[
                    dict(recipe=name, renderer_shader_preparation={'compiler': {'fixture': True}})
                    for name in ('eur', 'eur-catalogue', 'usa')])
                before = copy.deepcopy(original)
                catalog, items = self.bind(original, pack, [manifest], helper)
                self.assertEqual(original, before)
                package = root / 'relocated Forge'
                for item in items:
                    destination = package / item['path']
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(item['source'], destination)
                for title in catalog['titles']:
                    self.assertIn('shader_preparation', title)
                    checked_file(package, title['shader_preparation']['qualification'])
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
                self.bind(catalog, pack, [manifest], helper)
            pack.write_bytes(b'not a portable pack; GPU cache fixture')
            with self.assertRaises(ValueError):
                self.bind(catalog, pack, [manifest], helper)

    def test_unqualified_or_stale_corpus_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pack, manifest, helper = self.fixture(root)
            catalog = dict(target=WINDOWS.target, titles=[dict(recipe='eur', renderer_shader_preparation={})])
            self.bind(catalog, pack, [manifest], helper)
            (self.sources / 'generator.cpp').write_text('generator v2', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Stale shader corpus'):
                self.bind(catalog, pack, [manifest], helper)
            (self.sources / 'generator.cpp').write_text('generator', encoding='utf-8')
            (self.sources / 'runtime.cpp').write_text('runtime v2', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Stale variant recordings'):
                self.bind(catalog, pack, [manifest], helper)
            (self.sources / 'runtime.cpp').write_text('runtime', encoding='utf-8')
            other = root / 'other.o3ps'
            other.write_bytes(encode(3, {(2, 1, 2, 5): b'\x03\x02\x23\x07'}))
            with self.assertRaisesRegex(ValueError, 'differs from its qualification'):
                self.bind(catalog, other, [manifest], helper)


if __name__ == '__main__':
    unittest.main()
