#include "FrameWorker.h"

FrameWorker::FrameWorker(std::function<void()> task)
    : task(std::move(task)), thread(&FrameWorker::loop, this)
{
}

FrameWorker::~FrameWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_one();
    thread.join();
}

void FrameWorker::start()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        hasWork = true;
    }
    wake.notify_one();
}

void FrameWorker::wait()
{
    std::unique_lock<std::mutex> lock(mutex);
    done.wait(lock, [this]
              { return !hasWork; });
}

void FrameWorker::loop()
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this]
                      { return hasWork || stopping; });
            if (stopping)
                return;
        }

        task();
        ++framesBuilt;

        {
            std::lock_guard<std::mutex> lock(mutex);
            hasWork = false;
        }
        done.notify_one();
    }
}
