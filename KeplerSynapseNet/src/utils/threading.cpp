#include "utils/utils.h"
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <future>
#include <chrono>
#include <climits>
#include <memory>
#include <algorithm>

namespace synapse {
namespace utils {

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::atomic<int> activeTasks;
    std::atomic<uint64_t> completedTasks;
    size_t numThreads;

public:
    ThreadPool(size_t threads = std::thread::hardware_concurrency())
        : stop(false), activeTasks(0), completedTasks(0), numThreads(threads) {
        if (numThreads == 0) numThreads = 4;

        for (size_t i = 0; i < numThreads; i++) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] {
                            return stop || !tasks.empty();
                        });

                        if (stop && tasks.empty()) return;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    activeTasks++;
                    task();
                    activeTasks--;
                    completedTasks++;
                }
            });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    void shutdown() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void waitAll() {
        while (activeTasks > 0 || !tasks.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    size_t getThreadCount() const { return numThreads; }
    size_t getQueueSize() const { return tasks.size(); }
    int getActiveTasks() const { return activeTasks; }
    uint64_t getCompletedTasks() const { return completedTasks; }
    bool isStopped() const { return stop; }
};

class TaskScheduler {
private:
    struct ScheduledTask {
        std::function<void()> task;
        std::chrono::steady_clock::time_point nextRun;
        std::chrono::milliseconds interval;
        bool recurring;
        bool cancelled;
        std::string id;
    };

    std::vector<ScheduledTask> tasks;
    std::mutex mutex;
    std::condition_variable cv;
    std::thread schedulerThread;
    std::atomic<bool> running;

public:
    TaskScheduler() : running(false) {}

    ~TaskScheduler() {
        stop();
    }

    void start() {
        running = true;
        schedulerThread = std::thread(&TaskScheduler::run, this);
    }

    void stop() {
        running = false;
        cv.notify_all();
        if (schedulerThread.joinable()) {
            schedulerThread.join();
        }
    }

    std::string scheduleOnce(std::function<void()> task, std::chrono::milliseconds delay) {
        std::lock_guard<std::mutex> lock(mutex);

        ScheduledTask st;
        st.task = task;
        st.nextRun = std::chrono::steady_clock::now() + delay;
        st.interval = std::chrono::milliseconds(0);
        st.recurring = false;
        st.cancelled = false;
        st.id = generateId();

        tasks.push_back(st);
        cv.notify_one();

        return st.id;
    }

    std::string scheduleRecurring(std::function<void()> task, std::chrono::milliseconds interval) {
        std::lock_guard<std::mutex> lock(mutex);

        ScheduledTask st;
        st.task = task;
        st.nextRun = std::chrono::steady_clock::now() + interval;
        st.interval = interval;
        st.recurring = true;
        st.cancelled = false;
        st.id = generateId();

        tasks.push_back(st);
        cv.notify_one();

        return st.id;
    }

    void cancel(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& task : tasks) {
            if (task.id == id) {
                task.cancelled = true;
                break;
            }
        }
    }

    void cancelAll() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& task : tasks) {
            task.cancelled = true;
        }
    }

private:
    void run() {
        while (running) {
            std::unique_lock<std::mutex> lock(mutex);

            tasks.erase(
                std::remove_if(tasks.begin(), tasks.end(),
                    [](const ScheduledTask& t) { return t.cancelled; }),
                tasks.end());

            if (tasks.empty()) {
                cv.wait_for(lock, std::chrono::seconds(1));
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            auto nextTask = std::min_element(tasks.begin(), tasks.end(),
                [](const ScheduledTask& a, const ScheduledTask& b) {
                    return a.nextRun < b.nextRun;
                });

            if (nextTask->nextRun <= now) {
                auto task = nextTask->task;
                if (nextTask->recurring) {
                    nextTask->nextRun = now + nextTask->interval;
                } else {
                    nextTask->cancelled = true;
                }

                lock.unlock();
                task();
            } else {
                cv.wait_until(lock, nextTask->nextRun);
            }
        }
    }

    std::string generateId() {
        static std::atomic<uint64_t> counter(0);
        return "task_" + std::to_string(counter++);
    }
};

class WorkerGroup {
private:
    std::vector<std::thread> workers;
    std::atomic<bool> running;
    std::function<void(int)> workerFunc;
    int numWorkers;

public:
    WorkerGroup(int count = 4) : running(false), numWorkers(count) {}

    ~WorkerGroup() {
        stop();
    }

    void start(std::function<void(int)> func) {
        workerFunc = func;
        running = true;

        for (int i = 0; i < numWorkers; i++) {
            workers.emplace_back([this, i] {
                while (running) {
                    workerFunc(i);
                }
            });
        }
    }

    void stop() {
        running = false;
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }

    bool isRunning() const { return running; }
    int getWorkerCount() const { return numWorkers; }
};

class Semaphore {
private:
    std::mutex mutex;
    std::condition_variable cv;
    int count;
    int maxCount;

public:
    Semaphore(int initial = 0, int max = INT_MAX) 
        : count(initial), maxCount(max) {}

    void acquire() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return count > 0; });
        count--;
    }

    bool tryAcquire() {
        std::lock_guard<std::mutex> lock(mutex);
        if (count > 0) {
            count--;
            return true;
        }
        return false;
    }

    bool tryAcquireFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        if (cv.wait_for(lock, timeout, [this] { return count > 0; })) {
            count--;
            return true;
        }
        return false;
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        if (count < maxCount) {
            count++;
            cv.notify_one();
        }
    }

    int getCount() const { return count; }
};

class ReadWriteLock {
private:
    std::mutex mutex;
    std::condition_variable readCV;
    std::condition_variable writeCV;
    int readers;
    int writers;
    int waitingWriters;

public:
    ReadWriteLock() : readers(0), writers(0), waitingWriters(0) {}

    void lockRead() {
        std::unique_lock<std::mutex> lock(mutex);
        readCV.wait(lock, [this] { return writers == 0 && waitingWriters == 0; });
        readers++;
    }

    void unlockRead() {
        std::unique_lock<std::mutex> lock(mutex);
        readers--;
        if (readers == 0) {
            writeCV.notify_one();
        }
    }

    void lockWrite() {
        std::unique_lock<std::mutex> lock(mutex);
        waitingWriters++;
        writeCV.wait(lock, [this] { return readers == 0 && writers == 0; });
        waitingWriters--;
        writers++;
    }

    void unlockWrite() {
        std::unique_lock<std::mutex> lock(mutex);
        writers--;
        if (waitingWriters > 0) {
            writeCV.notify_one();
        } else {
            readCV.notify_all();
        }
    }

    bool tryLockRead() {
        std::lock_guard<std::mutex> lock(mutex);
        if (writers == 0 && waitingWriters == 0) {
            readers++;
            return true;
        }
        return false;
    }

    bool tryLockWrite() {
        std::lock_guard<std::mutex> lock(mutex);
        if (readers == 0 && writers == 0) {
            writers++;
            return true;
        }
        return false;
    }
};

class ReadLockGuard {
private:
    ReadWriteLock& lock;

public:
    ReadLockGuard(ReadWriteLock& l) : lock(l) {
        lock.lockRead();
    }

    ~ReadLockGuard() {
        lock.unlockRead();
    }

    ReadLockGuard(const ReadLockGuard&) = delete;
    ReadLockGuard& operator=(const ReadLockGuard&) = delete;
};

class WriteLockGuard {
private:
    ReadWriteLock& lock;

public:
    WriteLockGuard(ReadWriteLock& l) : lock(l) {
        lock.lockWrite();
    }

    ~WriteLockGuard() {
        lock.unlockWrite();
    }

    WriteLockGuard(const WriteLockGuard&) = delete;
    WriteLockGuard& operator=(const WriteLockGuard&) = delete;
};

template<typename T>
class BlockingQueue {
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    size_t maxSize;
    std::atomic<bool> closed;

public:
    BlockingQueue(size_t max = SIZE_MAX) : maxSize(max), closed(false) {}

    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex);
        notFull.wait(lock, [this] { return queue.size() < maxSize || closed; });

        if (closed) return false;

        queue.push(item);
        notEmpty.notify_one();
        return true;
    }

    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mutex);
        notFull.wait(lock, [this] { return queue.size() < maxSize || closed; });

        if (closed) return false;

        queue.push(std::move(item));
        notEmpty.notify_one();
        return true;
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex);
        notEmpty.wait(lock, [this] { return !queue.empty() || closed; });

        if (queue.empty()) return false;

        item = std::move(queue.front());
        queue.pop();
        notFull.notify_one();
        return true;
    }

    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;

        item = std::move(queue.front());
        queue.pop();
        notFull.notify_one();
        return true;
    }

    bool tryPopFor(T& item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        if (!notEmpty.wait_for(lock, timeout, [this] { return !queue.empty() || closed; })) {
            return false;
        }

        if (queue.empty()) return false;

        item = std::move(queue.front());
        queue.pop();
        notFull.notify_one();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex);
        closed = true;
        notEmpty.notify_all();
        notFull.notify_all();
    }

    bool isClosed() const { return closed; }
    size_t size() const { return queue.size(); }
    bool empty() const { return queue.empty(); }
};

class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }

    bool tryLock() {
        return !flag.test_and_set(std::memory_order_acquire);
    }
};

class SpinLockGuard {
private:
    SpinLock& lock;

public:
    SpinLockGuard(SpinLock& l) : lock(l) {
        lock.lock();
    }

    ~SpinLockGuard() {
        lock.unlock();
    }

    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
};

class Barrier {
private:
    std::mutex mutex;
    std::condition_variable cv;
    int count;
    int waiting;
    int generation;

public:
    Barrier(int n) : count(n), waiting(0), generation(0) {}

    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        int gen = generation;
        waiting++;

        if (waiting == count) {
            generation++;
            waiting = 0;
            cv.notify_all();
        } else {
            cv.wait(lock, [this, gen] { return gen != generation; });
        }
    }

    void reset(int n) {
        std::lock_guard<std::mutex> lock(mutex);
        count = n;
        waiting = 0;
        generation++;
    }
};

class Latch {
private:
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> count;

public:
    Latch(int n) : count(n) {}

    void countDown() {
        if (--count <= 0) {
            cv.notify_all();
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return count <= 0; });
    }

    bool tryWait() {
        return count <= 0;
    }

    int getCount() const { return count; }
};

class OnceFlag {
private:
    std::once_flag flag;

public:
    template<typename Callable, typename... Args>
    void callOnce(Callable&& f, Args&&... args) {
        std::call_once(flag, std::forward<Callable>(f), std::forward<Args>(args)...);
    }
};

}
}
