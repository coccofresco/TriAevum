import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class ShutdownRequestTests(unittest.TestCase):
    def test_real_signal_defers_destruction_to_normal_scope_exit(self):
        compiler = os.environ.get('TRIAEVUM_TEST_CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.skipTest('C++ compiler required')
        include = Path(__file__).resolve().parents[2] / 'runtime/three_ds_recomp/include'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'signal.cpp'
            source.write_text(r'''
#include "ship/utils/ShutdownRequest.h"
#include <csignal>
#include <cstdlib>
int destroyed = 0;
struct Scope { ~Scope() { ++destroyed; } };
int main() {
    Ship::ShutdownRequest::Reset();
    if (Ship::ShutdownRequest::Requested()) return 1;
    std::signal(SIGTERM, Ship::ShutdownRequest::HandleSignal);
    {
        Scope live;
        std::raise(SIGTERM);
        if (!Ship::ShutdownRequest::Requested() || destroyed) return 2;
        std::signal(SIGTERM, Ship::ShutdownRequest::HandleSignal);
        std::raise(SIGTERM);
        if (!Ship::ShutdownRequest::Requested() || destroyed) return 3;
    }
    if (destroyed != 1) return 4;
    Ship::ShutdownRequest::Reset();
    return Ship::ShutdownRequest::Requested() ? 5 : 0;
}
''')
            binary = root / ('signal.exe' if os.name == 'nt' else 'signal')
            subprocess.run([compiler, '-std=c++17', '-I', str(include), str(source),
                            '-o', str(binary)], check=True, capture_output=True, timeout=60)
            subprocess.run([str(binary)], check=True, capture_output=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
