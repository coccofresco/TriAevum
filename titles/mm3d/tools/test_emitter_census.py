import unittest
from types import SimpleNamespace
from emitter_census import material_roles, setup_meshes
import struct

class CensusTests(unittest.TestCase):
    def model(self, names):
        return SimpleNamespace(meshes=[(0,0),(1,0)], materials=[([0,1,2], True)],
                               textures=[(name,8,8,0,0,b'encoded') for name in names])

    def test_all_slots_and_unknowns(self):
        rows=material_roles(self.model(['wall','magic','torch']))
        self.assertEqual(len(rows),1)
        self.assertEqual([s['reason'] for s in rows[0]['slots']],
                         ['unknown-authored-role','secondary-role-needs-combiner-UV-proof',
                          'excluded-fixture-or-non-emitter'])
        self.assertEqual(rows[0]['slots'][1]['category'],'magic-glow')

    def test_primary_does_not_promote_unknown_secondary(self):
        row=material_roles(self.model(['mado_01','ordinary','unknown']))[0]
        self.assertEqual(row['slots'][0]['reason'],'primary-semantic-candidate')
        self.assertIsNone(row['slots'][1]['category'])

    def test_setup_mesh_identity(self):
        data=bytearray(88)
        data[16:18]=bytes([0x18,1]);struct.pack_into('<I',data,20,48)
        data[24]=0x0A;struct.pack_into('<I',data,28,500);data[32]=0x14
        struct.pack_into('<I',data,64,56)
        data[72]=0x0A;struct.pack_into('<I',data,76,500);data[80]=0x14
        self.assertTrue(setup_meshes(data)['invariant'])
        struct.pack_into('<I',data,76,600)
        self.assertFalse(setup_meshes(data)['invariant'])
        with self.assertRaises(ValueError):setup_meshes(data[:80])

    def test_bad_index_fails_closed(self):
        model=self.model(['lamp','wall','glow'])
        model.materials=[([3,-1,-1],False)]
        with self.assertRaises(ValueError):material_roles(model)

if __name__=='__main__': unittest.main()
