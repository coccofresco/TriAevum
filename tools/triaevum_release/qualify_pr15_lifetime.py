"""Bounded PR #15 worker-lifetime admission test; never loads a game or GPU.

Extracts the exact reviewed worker from Git, without adding it to the runtime.
The fixture observes a destruction marker, not freed storage: no intentional UB.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HEAD = "99454b757805c5580a2cada2ac214870fcd52693"
SOURCE = "tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp"
BLOB = "f6d8ca94c52bee8e79616c551bc341c60d7280fa"
FIXTURE = r'''
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

// WORKER

// A frame's destruction marker remains alive outside the tested scope. The
// worker does not dereference the destroyed frame or its reference captures.
bool presentFailure(bool joinedBeforeUnwind) {
    GuestWorker worker(true);
    std::atomic<bool> frameAlive{true};
    std::promise<void> release, started;
    auto go = release.get_future();
    auto ready = started.get_future();
    bool workerSawLiveFrame = false;
    struct Frame {
        std::atomic<bool>& alive;
        std::promise<void>& release;
        bool guarded;
        ~Frame() { alive = false; if (!guarded) release.set_value(); }
    };
    struct JoinScope {
        GuestWorker& worker;
        std::promise<void>& release;
        bool guarded;
        ~JoinScope() noexcept {
            if (guarded) {
                release.set_value();
                try { worker.Wait(); } catch (...) {}
            }
        }
    };
    try {
        Frame frame{frameAlive, release, joinedBeforeUnwind};
        JoinScope join{worker, release, joinedBeforeUnwind};
        worker.Run([&] {
            started.set_value();
            go.wait();
            workerSawLiveFrame = frameAlive.load();
        });
        ready.wait();
        throw std::runtime_error("injected presentation failure");
    } catch (const std::runtime_error&) {
        worker.Wait();
    }
    return workerSawLiveFrame;
}

int main() {
    unsigned unguardedExpired = 0, guardedAlive = 0;
    for (unsigned i = 0; i != 100; ++i) {
        unguardedExpired += !presentFailure(false);
        guardedAlive += presentFailure(true);
    }
    std::cout << "{\"unguarded_frame_expired\":" << unguardedExpired
              << ",\"guarded_frame_alive\":" << guardedAlive << "}\n";
    return unguardedExpired == 100 && guardedAlive == 100 ? 0 : 1;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--repository", type=Path)
    inputs.add_argument("--source", type=Path, help="Exact pinned Git blob, checked before compilation")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    data = args.source.read_bytes() if args.source else subprocess.check_output(
        ["git", "show", HEAD + ":" + SOURCE], cwd=args.repository, timeout=15)
    if hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() != BLOB:
        raise ValueError("Worker source differs from the reviewed PR blob")
    text = data.decode("utf-8")
    start = text.index("  struct GuestWorker {")
    end = text.index("  } guestWorker(hostArgs.GuestThread);", start)
    worker = text[start:end] + "  };"
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.output / "worker_lifetime.cpp"
    source.write_text(FIXTURE.replace("// WORKER", worker), encoding="utf-8")
    binary = args.output / "worker_lifetime"
    subprocess.run([args.compiler, "-std=c++20", "-O2", "-pthread", str(source), "-o", str(binary)],
                   check=True, timeout=60)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10, check=True)
    report = {"pr_head": HEAD, "source": SOURCE, "iterations": 100,
              "result": json.loads(result.stdout), "gameplay_threading_tested": False,
              "scope": "destruction ordering only; not a data-race or performance proof"}
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
