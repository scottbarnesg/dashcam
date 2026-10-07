#include <condition_variable>
#include <mutex>
#include <queue>

#ifndef QUEUE_H
#define QUEUE_H

// Thread-safe bounded queue. Once close()d, pop() drains remaining items then
// returns default-constructed T values so blocked consumers can shut down.
template <typename T>
class SafeQueue {
    public:
        explicit SafeQueue(std::size_t capacity = 0) : capacity(capacity) {
        }

        // Returns false (and drops the item) if the queue is at capacity or closed.
        bool push(T t) {
            std::unique_lock<std::mutex> lock(mut);
            if (closed || (capacity > 0 && queue.size() >= capacity)) {
                return false;
            }
            queue.push(std::move(t));
            cond.notify_one();
            return true;
        }

        // Blocks until an item is available or the queue is closed.
        // Returns default-constructed T when closed and drained.
        T pop() {
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock, [this]() { return !queue.empty() || closed; });
            if (queue.empty()) {
                return T{};
            }
            T result = std::move(queue.front());
            queue.pop();
            return result;
        }

        T front() {
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock, [this]() { return !queue.empty() || closed; });
            if (queue.empty()) {
                return T{};
            }
            return queue.front();
        }

        std::size_t size() {
            std::unique_lock<std::mutex> lock(mut);
            return queue.size();
        }

        bool empty() {
            return size() == 0;
        }

        void close() {
            std::unique_lock<std::mutex> lock(mut);
            closed = true;
            cond.notify_all();
        }

    private:
        std::queue<T> queue;
        std::mutex mut;
        std::condition_variable cond;
        std::size_t capacity = 0; // 0 = unbounded
        bool closed = false;
};

#endif
