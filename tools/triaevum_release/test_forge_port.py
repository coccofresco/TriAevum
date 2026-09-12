"""Run the focused Windows/Linux installer suite, without compiler benchmarks."""

from pathlib import Path
import sys
import unittest


def main():
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root))
    sys.path.insert(0, str(root / "tools/triaevum_release"))
    modules = (
        "test_release_platform", "test_precompiled_titles", "test_forge_gui",
        "test_installed_runtime", "test_installation_context", "test_tam_builder",
        "test_linux_precompiled_catalog", "test_worker_job", "test_ctr_rom",
        "test_input_adapters", "test_topscreen_assets", "test_activation_transaction",
        "test_native_process", "test_product_contract",
        "test_https_transport",
        "test_linux_forge_qualification",
    )
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromNames(modules))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
