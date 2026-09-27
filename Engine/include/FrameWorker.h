#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

class FrameWorker
{
public:
    explicit FrameWorker(std::function<void()> task);
    ~FrameWorker();

    FrameWorker(const FrameWorker &) = delete;
    FrameWorker &operator=(const FrameWorker &) = delete;

    void start();
    void wait();

    uint64_t getFramesBuilt() const { return framesBuilt; }

private:
    void loop();

    std::function<void()> task;
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable done;
    bool hasWork = false;
    bool stopping = false;
    std::atomic<uint64_t> framesBuilt{0};
    std::thread thread;
};
