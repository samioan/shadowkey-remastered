#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sk {

// M119: a fixed set of worker threads that run one job over `count` indices
// -- the renderer's horizontal bands at a higher Resolution. The calling
// thread works too, and Run returns once every index is done.
//
// Indices are handed out one at a time from a shared counter, so a band full
// of wall and creature does not hold up the frame while the threads that
// drew sky sit idle.
class BandPool {
public:
    explicit BandPool(int workers) {
        for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { WorkerLoop(); });
    }
    ~BandPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        for (std::thread& t : threads_) t.join();
    }
    BandPool(const BandPool&) = delete;
    BandPool& operator=(const BandPool&) = delete;

    int threadCount() const { return static_cast<int>(threads_.size()) + 1; }

    void Run(int count, const std::function<void(int)>& job) {
        if (count <= 0) return;
        {
            // The job and counters are only ever written while no worker is
            // inside Drain -- a straggler from the last Run must be out
            // before they change under it.
            std::unique_lock<std::mutex> lock(mutex_);
            done_.wait(lock, [this] { return active_ == 0; });
            job_ = &job;
            count_ = count;
            next_.store(0);
            remaining_ = count;
            ++generation_;
        }
        wake_.notify_all();
        Drain();
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return remaining_ == 0 && active_ == 0; });
    }

    // One pool for the process, sized to the machine, created on first use
    // and deliberately never destroyed: its threads are parked on a
    // condition variable, and the OS reclaims them at exit without the
    // static-destruction-order questions a destructor would raise.
    static BandPool& Shared() {
        static BandPool* pool = new BandPool(DefaultWorkers());
        return *pool;
    }

private:
    static int DefaultWorkers() {
        const unsigned hw = std::thread::hardware_concurrency();
        const int n = hw == 0 ? 4 : static_cast<int>(hw);
        return std::max(0, std::min(n, 16) - 1);
    }

    void Drain() {
        for (;;) {
            const int i = next_.fetch_add(1);
            if (i >= count_) return;
            (*job_)(i);
            std::lock_guard<std::mutex> lock(mutex_);
            if (--remaining_ == 0) done_.notify_all();
        }
    }

    void WorkerLoop() {
        unsigned long long seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
                if (stop_) return;
                seen = generation_;
                ++active_;
            }
            Drain();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (--active_ == 0) done_.notify_all();
            }
        }
    }

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    const std::function<void(int)>* job_ = nullptr;
    int count_ = 0;
    std::atomic<int> next_{0};
    int remaining_ = 0;
    int active_ = 0;  // workers inside Drain
    unsigned long long generation_ = 0;
    bool stop_ = false;
};

}  // namespace sk
