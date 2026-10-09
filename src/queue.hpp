#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

#ifndef QUEUE_H
#define QUEUE_H

// Thread-safe bounded queue. Once close()d, pop() drains remaining items then
// returns default-constructed T values so blocked consumers can shut down.
// Overflow policy: DropNewest (default) rejects incoming items; DropOldest
// evicts the front so consumers always hold the freshest items (live video
// stages). Overload rejections are counted in dropCount(); pushes after
// close() are rejected silently (shutdown races, not overload).
template <typename T>
class SafeQueue {
    public:
        enum class Overflow { DropNewest, DropOldest };

        explicit SafeQueue(std::size_t capacity = 0, Overflow overflow = Overflow::DropNewest)
            : capacity(capacity), overflow(overflow) {
        }

        // Returns false if the queue is closed (not counted) or full
        // (counted as a drop; DropOldest evicts the front item instead of
        // rejecting the new one).
        bool push(T t) {
            std::lock_guard<std::mutex> lock(mut);
            if (closed) {
                return false;
            }
            if (capacity > 0 && queue.size() >= capacity) {
                drops.fetch_add(1);
                if (overflow == Overflow::DropNewest) {
                    return false;
                }
                queue.pop(); // Make room: consumers keep the freshest item.
            }
            queue.push(std::move(t));
            cond.notify_one();
            return true;
        };

        // Items rejected since construction.
        std::size_t dropCount() const {
            return drops.load();
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
        Overflow overflow = Overflow::DropNewest;
        std::atomic<std::size_t> drops{0};
        bool closed = false;
    };

#endif
