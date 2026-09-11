import unittest
import numpy as np
from measure_grass_coverage import measure_region


class GrassCoverageTests(unittest.TestCase):
    def test_new_coverage_and_gaps(self):
        base=np.zeros((16,16,3),dtype=np.uint8)
        old=base.copy(); old[:8]=255
        new=old.copy(); new[8:]=128
        score=measure_region(old,new,base,(0,0,1,1))
        self.assertEqual(score["reference_coverage"],0.5)
        self.assertEqual(score["candidate_coverage"],1)
        self.assertEqual(score["gain_points"],50)
        self.assertEqual(score["reference_empty_tiles"],0.5)
        self.assertEqual(score["candidate_empty_tiles"],0)
        self.assertEqual(score["lost_reference_fraction"],0)

    def test_only_selected_ground_and_unsigned_subtraction(self):
        base=np.full((16,16,3),255,dtype=np.uint8)
        old=base.copy(); old[:8]=0
        new=old.copy(); new[8:]=0
        score=measure_region(old,new,base,(0,0,1,0.5))
        self.assertEqual(score["gain_points"],0)
        self.assertEqual(score["reference_coverage"],1)

    def test_rejects_wrong_frame_and_region(self):
        image=np.zeros((16,16,3),dtype=np.uint8)
        with self.assertRaises(ValueError):
            measure_region(image,image[:8],image,(0,0,1,1))
        with self.assertRaises(ValueError):
            measure_region(image,image,image,(1,0,0,1))
