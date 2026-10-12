#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include <opencv2/core.hpp>

#include "frame.hpp"

#ifndef FRAME_POOL_HPP
#define FRAME_POOL_HPP

// Recycling buffer pool for capture frames.
//
// Background (field OOM post-mortem): at capture rate the pipeline churned
// ~4.5 MB of fresh malloc/free traffic per frame (the assembled NV12 copy,
// the orientation output, the motion-diff buffers), freed on different
// threads than they were allocated on. glibc cannot serve multi-MB blocks
// from its per-thread heaps, so they cycle through mmap; when a block is
// freed on an arena the next requesting thread cannot reuse, virtual memory
// grows without bound (~480 MB of stale 1.3 MB mappings before the kernel
// OOM-killed the process on a 415 MB Pi). Pre-allocating the working set
// once removes the churn entirely: steady state performs zero frame-sized
// allocations.
//
// A lease is a shared_ptr<RawFrame> whose custom deleter returns the object
// (with its cv::Mat buffer) to the pool. Queues and stages store SharedFrame
// leases; the buffer reappears from acquire() once the last reference dies.
// The pool state is shared_ptr-held so lease deleters stay valid even if
// the pool object itself goes away first.
//
// acquire() never blocks: once `capacity` buffers are outstanding it hands
// back nothing and counts a stall, so a wedged consumer degrades to dropped
// frames at the source instead of unbounded allocation — the correct
// failure mode for a dashcam.
class FramePool {
    public:
        FramePool(int rows, int cols, std::size_t capacity) : state(std::make_shared<State>()) {
            state->rows = rows;
            state->cols = cols;
            state->capacity = capacity;
        }

        // Reuse a recycled buffer, or allocate a new one while under
        // capacity. Returns nullptr when every buffer is outstanding (the
        // caller drops the frame; stalls() counts these).
        std::shared_ptr<RawFrame> acquire() {
            std::unique_ptr<RawFrame> frame;
            {
                std::lock_guard<std::mutex> lock(state->mut);
                if (!state->freeList.empty()) {
                    frame = std::move(state->freeList.back());
                    state->freeList.pop_back();
                } else if (state->allocated < state->capacity) {
                    frame = std::make_unique<RawFrame>();
                    frame->data = cv::Mat(state->rows, state->cols, CV_8UC1);
                    state->allocated++;
                }
            }
            if (!frame) {
                stalls.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
            frame->format = PixelFormat::BGR; // Caller overwrites.
            frame->colorSpace = ColorSpace::Unspecified;
            frame->timestamp = {};
            std::shared_ptr<State> keeper = state;
            return std::shared_ptr<RawFrame>(frame.release(), [keeper](RawFrame* raw) {
                std::unique_ptr<RawFrame> owned(raw);
                std::lock_guard<std::mutex> lock(keeper->mut);
                if (keeper->freeList.size() < keeper->capacity) {
                    keeper->freeList.push_back(std::move(owned));
                } else {
                    owned.reset(); // Over-capacity surplus; let it go.
                }
            });
        }

        // Frames dropped because every pooled buffer was still in flight.
        std::size_t stallsCount() const { return stalls.load(std::memory_order_relaxed); }

        // Buffers allocated so far (grows to the peak in-flight count,
        // never beyond the capacity).
        std::size_t allocated() const {
            std::lock_guard<std::mutex> lock(state->mut);
            return state->allocated;
        }

    private:
        struct State {
            std::mutex mut;
            std::vector<std::unique_ptr<RawFrame>> freeList;
            std::size_t allocated = 0;
            int rows;
            int cols;
            std::size_t capacity;
        };
        std::shared_ptr<State> state;
        std::atomic<std::size_t> stalls{0};
};

#endif
