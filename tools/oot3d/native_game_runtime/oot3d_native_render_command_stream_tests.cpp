#include "oot3d_native_render_command_stream.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Oot3dNativeGame::NativeRenderCommandStream;
using Mode = NativeRenderCommandStream::Mode;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "oot3d_native_render_command_stream_tests: " << message << '\n';
        std::exit(1);
    }
}

void TestInlineRunsAtSubmission() {
    NativeRenderCommandStream stream(Mode::Inline);
    int value = 0;
    bool inside = false;
    stream.Submit([&] { value = 1; inside = stream.InCommand(); });
    Require(value == 1 && inside && !stream.InCommand(),
            "inline command did not run at submission inside a command scope");
    bool threw = false;
    try {
        stream.Submit([] { throw std::runtime_error("inline"); });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "inline command exception did not reach the submitter");
}

void TestWorkerKeepsProgramOrder() {
    NativeRenderCommandStream stream(Mode::Worker, 2U);
    std::vector<int> order;
    const auto mainThread = std::this_thread::get_id();
    std::atomic<bool> offThread{true};
    for (int i = 0; i < 1000; ++i) {
        stream.Submit([&, i] {
            order.push_back(i);
            if (std::this_thread::get_id() == mainThread) offThread = false;
        }, i % 10 == 9);
    }
    int observed = -1;
    stream.Run([&] { observed = static_cast<int>(order.size()); });
    Require(observed == 1000, "Run did not execute after every earlier command");
    for (int i = 0; i < 1000; ++i) {
        Require(order[static_cast<size_t>(i)] == i, "worker changed command order");
    }
    Require(offThread.load(), "worker commands ran on the submitting thread");
    stream.Drain();
}

void TestFramesInFlightBound() {
    NativeRenderCommandStream stream(Mode::Worker, 1U);
    std::atomic<bool> release{false};
    std::atomic<bool> secondSubmitted{false};
    stream.Submit([&] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }, true);
    std::thread producer([&] {
        stream.Submit([] {}, true);
        secondSubmitted = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Require(!secondSubmitted.load(),
            "a second frame was accepted while the first was still in flight");
    release = true;
    producer.join();
    Require(secondSubmitted.load(), "second frame was never accepted");
    stream.Drain();
}

// Input admission: the producer waits until the previous frame has ended,
// while commands that do not end a frame never hold it.
void TestWaitForFramesInFlight() {
    NativeRenderCommandStream inlineStream(Mode::Inline);
    inlineStream.WaitForFramesInFlight(0U);
    NativeRenderCommandStream stream(Mode::Worker, 1U);
    std::atomic<bool> release{false};
    std::atomic<bool> ended{false};
    std::atomic<bool> admitted{false};
    stream.Submit([&] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    stream.Submit([&] { ended = true; }, true);
    std::thread producer([&] {
        stream.WaitForFramesInFlight(1U);
        stream.WaitForFramesInFlight(0U);
        admitted = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Require(!admitted.load(), "the producer was admitted before the previous frame ended");
    release = true;
    producer.join();
    Require(admitted.load() && ended.load(), "admission did not follow the frame end");
    stream.Submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });
    stream.WaitForFramesInFlight(0U);
    stream.Drain();
}

void TestWorkerFailureIsRethrownOnce() {
    NativeRenderCommandStream stream(Mode::Worker);
    bool laterRan = false;
    stream.Submit([] { throw std::runtime_error("render failed"); });
    stream.Submit([&] { laterRan = true; });
    bool rethrown = false;
    try {
        stream.Drain();
    } catch (const std::runtime_error& error) {
        rethrown = std::string(error.what()) == "render failed";
    }
    Require(rethrown, "worker exception was not rethrown by Drain");
    Require(!laterRan, "commands after a failure still ran");
    int value = 0;
    stream.Run([&] { value = 7; });
    Require(value == 7, "stream was not usable after reporting its failure");
}

void TestModeSwitchDrainsAndDestructorRunsPending() {
    std::vector<int> order;
    {
        NativeRenderCommandStream stream(Mode::Worker);
        for (int i = 0; i < 50; ++i) stream.Submit([&, i] { order.push_back(i); });
        stream.SetMode(Mode::Inline);
        Require(order.size() == 50U, "switching to inline did not drain the worker");
        stream.Submit([&] { order.push_back(50); });
        Require(order.size() == 51U, "inline mode did not run immediately after a switch");
        stream.SetMode(Mode::Worker);
        for (int i = 51; i < 60; ++i) stream.Submit([&, i] { order.push_back(i); });
    }
    Require(order.size() == 60U, "destruction discarded pending commands");
    for (int i = 0; i < 60; ++i) {
        Require(order[static_cast<size_t>(i)] == i, "order changed across mode switches");
    }
}

// Only commands whose submitter waits for them may reach guest state.
void TestSynchronousCommandsAreDistinguished() {
    for (const Mode mode : {Mode::Inline, Mode::Worker}) {
        NativeRenderCommandStream stream(mode);
        bool submitted = true;
        bool ran = false;
        stream.Submit([&] { submitted = stream.InSynchronousCommand(); });
        std::thread::id runThread;
        stream.Run([&] {
            ran = stream.InSynchronousCommand();
            runThread = std::this_thread::get_id();
        });
        Require(runThread == std::this_thread::get_id(),
                "Run did not execute on the calling thread");
        Require(submitted == (mode == Mode::Inline) && ran &&
                    !stream.InSynchronousCommand(),
                "stream misreported a synchronous command");
    }
}

} // namespace

int main() {
    TestInlineRunsAtSubmission();
    TestWorkerKeepsProgramOrder();
    TestFramesInFlightBound();
    TestWaitForFramesInFlight();
    TestWorkerFailureIsRethrownOnce();
    TestModeSwitchDrainsAndDestructorRunsPending();
    TestSynchronousCommandsAreDistinguished();
    std::cout << "oot3d_native_render_command_stream_tests: ok\n";
    return 0;
}
