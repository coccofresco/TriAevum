import unittest
from pathlib import Path

from validate_product_run import validate_run


class ProductRunModeTests(unittest.TestCase):
    def test_rejects_interpolated_throughput_before_creating_fixture(self):
        for multiplier, throughput in ((0, False), (4, False), (2, True), (3, True)):
            with self.assertRaisesRegex(ValueError, "Throughput requires native x1"):
                validate_run(*(Path("unused") for _ in range(4)), multiplier=multiplier, throughput=throughput)
