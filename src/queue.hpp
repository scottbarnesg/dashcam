#include <condition_variable>
#include <queue>
#include <mutex>

#ifndef QUEUE_H
#define QUEUE_H

template <typename T>
class SafeQueue {
    public:
        void push(T t) {
            std::unique_lock<std::mutex> lock(mut);
            queue.push(t);
            cond.notify_one();
        };
        T pop() {
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock, [this]() { return !queue.empty(); });
            T result = queue.front();
            queue.pop();
            return result;
        };
        T front() {
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock, [this]() { return !queue.empty(); });
            T result = queue.front();
            return result;
        }
        int size() {
            std::unique_lock<std::mutex> lock(mut);
            return queue.size();
        }
        bool empty() {
            return size() == 0;
        }
        void waitForNewItem() {
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock);
        }
    private:
        std::queue<T> queue;
        std::mutex mut;
        std::condition_variable cond;
};

#endif