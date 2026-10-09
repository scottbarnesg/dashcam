#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "camera.hpp"
#include "driving.hpp"
#include "frame.hpp"
#include "motion.hpp"
#include "queue.hpp"
#include "video.hpp"

#ifndef PIPELINE_H
#define PIPELINE_H

// Staged capture pipeline: each stage runs on its own thread, connected by
// bounded queues, so a slow stage can never block frame intake. The single
// main loop this replaces stalled on motion detection (findContours at full
// resolution), which starved the video writer (~15 fps encoded of 30 fps
// captured) while the camera's own queue silently discarded the rest.
//
//   ingest (T1): Camera::captureImage() -> dispatch to both stage queues,
//       paced by the controller's capture interval. Never computes; only
//       shares frames (shared_ptr over the cv::Mat refcount).
//   motion (T2): MotionDetector + DrivingController (the expensive stage).
//       Owns recording start/stop decisions; creates the VideoWriter and
//       hands control events to T3 through the record queue.
//   record (T3): receives frames + start/stop events; owns the VideoWriter,
//       including its draining destructor (a segment close may block for an
//       encoder flush — that must never stall T2, let alone T1).
//
// Overflow policy: the motion queue is small and DropOldest (detection needs
// the freshest frame); the record queue is DropNewest and counted (a dropped
// frame is a gap in the recording, which must stay visible in the counters).
class Pipeline {
    public:
        struct Options {
            int motionThreshold = 10000;
            DrivingController::Params driving;
            std::filesystem::path videoDir = "videos";
            VideoWriter::SegmentOptions segments;
            std::size_t motionQueueCapacity = 4;
            std::size_t recordQueueCapacity = 16;
        };

        explicit Pipeline(std::unique_ptr<Camera> camera, Options options);
        ~Pipeline();

        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        void start();
        // Idempotent: requests stop, wakes every blocked stage, joins all
        // three threads, and logs stage drop counters. Safe from another
        // thread while run() blocks.
        void stop();
        // Blocks until stop() is called (the production main loop).
        void run();

        bool recording() const { return recordingActive.load(); }
        std::size_t motionStageDrops() const { return motionQueue.dropCount(); }
        std::size_t recordStageDrops() const { return recordQueue.dropCount(); }
        // Frames the camera backend itself discarded (expected while idle —
        // the ingest thread deliberately samples below sensor rate — and a
        // concern only during recording).
        std::size_t cameraQueueDrops() const;

    private:
        // One item on the record queue: a frame to write, a writer to adopt,
        // or an instruction to close the current writer. Events and frames
        // share one FIFO so lifecycle order is preserved by construction
        // (e.g. a frame pushed while a stop is queued still lands in the
        // still-open segment).
        struct RecordEvent {
            std::shared_ptr<const RawFrame> frame;
            std::unique_ptr<VideoWriter> startWriter;
            bool stop = false;
        };

        void ingestLoop();
        void motionLoop();
        void recorderLoop();

        std::unique_ptr<Camera> camera;
        Options options;
        MotionDetector detector;
        DrivingController controller;

        SafeQueue<std::shared_ptr<const RawFrame>> motionQueue;
        SafeQueue<RecordEvent> recordQueue;

        std::thread ingestThread, motionThread, recorderThread;
        std::atomic<bool> started{false};
        std::atomic<bool> stopRequested{false};
        std::atomic<bool> recordingActive{false};
        std::atomic<long long> captureIntervalUs{200000}; // 5 fps idle default
        std::mutex stopMutex;
        std::mutex runMutex;
        std::condition_variable runCv;
};

#endif
