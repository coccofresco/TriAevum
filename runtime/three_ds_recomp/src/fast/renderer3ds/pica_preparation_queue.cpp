#include "fast/renderer3ds/pica_preparation_queue.h"

#include <algorithm>
#include <cstdio>
#include <exception>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Fast::Renderer3ds {

PicaPreparationQueue::PicaPreparationQueue(unsigned threads) {
    mThreads.reserve(threads);
    for (unsigned index = 0; index < threads; ++index) mThreads.emplace_back([this] { Worker(); });
}

PicaPreparationQueue::~PicaPreparationQueue() {
    Drain();
    {
        std::lock_guard lock(mMutex);
        mStopping = true;
    }
    mWake.notify_all();
    for (auto& thread : mThreads) thread.join();
}

std::shared_ptr<PicaPreparationTask> PicaPreparationQueue::Submit(std::function<void()> work) {
    auto task = std::make_shared<PicaPreparationTask>(std::move(work));
    if (mThreads.empty()) {
        Run(*task);
        task->mState.store(PicaPreparationTask::State::Done, std::memory_order_release);
        return task;
    }
    {
        std::lock_guard lock(mMutex);
        mQueue.push_back(task);
    }
    mWake.notify_one();
    return task;
}

void PicaPreparationQueue::Run(PicaPreparationTask& task) {
    try {
        task.mWork();
    } catch (const std::exception& exception) {
        // The work records its own failure; a throw here is a contract error.
        std::fprintf(stderr, "TRIAEVUM_PREPARATION_TASK_FAILED %s\n", exception.what());
    } catch (...) {
        std::fprintf(stderr, "TRIAEVUM_PREPARATION_TASK_FAILED unknown\n");
    }
    task.mWork = nullptr;
}

void PicaPreparationQueue::Complete(PicaPreparationTask& task) {
    if (task.Done()) return;
    std::unique_lock lock(mMutex);
    const auto queued = std::find_if(mQueue.begin(), mQueue.end(),
        [&task](const auto& entry) { return entry.get() == &task; });
    if (queued != mQueue.end()) {
        const auto owned = *queued;
        mQueue.erase(queued);
        owned->mState.store(PicaPreparationTask::State::Running, std::memory_order_release);
        ++mRunning;
        lock.unlock();
        Run(*owned);
        lock.lock();
        --mRunning;
        owned->mState.store(PicaPreparationTask::State::Done, std::memory_order_release);
        mFinished.notify_all();
        return;
    }
    mFinished.wait(lock, [&task] { return task.Done(); });
}

void PicaPreparationQueue::Drain() {
    for (;;) {
        std::shared_ptr<PicaPreparationTask> task;
        {
            std::unique_lock lock(mMutex);
            if (!mQueue.empty()) {
                task = mQueue.front();
                mQueue.pop_front();
                task->mState.store(PicaPreparationTask::State::Running, std::memory_order_release);
                ++mRunning;
            } else {
                mFinished.wait(lock, [this] { return mRunning == 0 || !mQueue.empty(); });
                if (mQueue.empty()) return;
                continue;
            }
        }
        Run(*task);
        std::lock_guard lock(mMutex);
        --mRunning;
        task->mState.store(PicaPreparationTask::State::Done, std::memory_order_release);
        mFinished.notify_all();
    }
}

size_t PicaPreparationQueue::Pending() const {
    std::lock_guard lock(mMutex);
    return mQueue.size() + mRunning;
}

void LowerPicaPreparationThreadPriority() {
#ifdef _WIN32
    // Preparation must never take a core from the guest or render threads.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

void PicaPreparationQueue::Worker() {
    LowerPicaPreparationThreadPriority();
    for (;;) {
        std::shared_ptr<PicaPreparationTask> task;
        {
            std::unique_lock lock(mMutex);
            mWake.wait(lock, [this] { return mStopping || !mQueue.empty(); });
            if (mQueue.empty()) return;
            task = mQueue.front();
            mQueue.pop_front();
            task->mState.store(PicaPreparationTask::State::Running, std::memory_order_release);
            ++mRunning;
        }
        Run(*task);
        {
            std::lock_guard lock(mMutex);
            --mRunning;
            task->mState.store(PicaPreparationTask::State::Done, std::memory_order_release);
        }
        mFinished.notify_all();
    }
}

} // namespace Fast::Renderer3ds
