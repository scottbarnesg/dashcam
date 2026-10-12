#include "pipeline.hpp"

#include <algorithm>
#include <iostream>

namespace {
using Clock = std::chrono::system_clock;

constexpr long long kIdleIntervalUs = 200000; // 5 fps until the controller speaks.
} // namespace

Pipeline::Pipeline(std::unique_ptr<Camera> cam, Options opts)
    : camera(std::move(cam)),
      options(std::move(opts)),
      detector(this->options.motionThreshold),
      controller(this->options.driving),
      motionQueue(this->options.motionQueueCapacity, SafeQueue<SharedFrame>::Overflow::DropOldest),
      recordQueue(this->options.recordQueueCapacity, SafeQueue<RecordEvent>::Overflow::DropNewest) {
    // Driving mode never idles (item 10): capture starts at the recording rate.
    double startFps = this->options.driving.mode == OperatingMode::Driving ? this->options.driving.recordingFps
                                                                           : this->options.driving.idleFps;
    captureIntervalUs.store(static_cast<long long>(1e6 / startFps));
}

Pipeline::~Pipeline() {
    stop();
}

std::size_t Pipeline::cameraQueueDrops() const {
    return camera->queueDrops();
}

void Pipeline::start() {
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) {
        return;
    }
    ingestThread = std::thread(&Pipeline::ingestLoop, this);
    motionThread = std::thread(&Pipeline::motionLoop, this);
    recorderThread = std::thread(&Pipeline::recorderLoop, this);
}

void Pipeline::run() {
    start();
    std::unique_lock<std::mutex> lock(runMutex);
    runCv.wait(lock, [this] { return stopRequested.load(); });
}

void Pipeline::stop() {
    std::lock_guard<std::mutex> lock(stopMutex);
    bool was = stopRequested.exchange(true);
    // Wake every parked consumer. Order: the camera (ingest thread), then the
    // two queues. Joins are ordered ingest -> motion -> recorder so frame
    // producers are done before the consumer drains the record queue.
    camera->stop();
    motionQueue.close();
    recordQueue.close();
    if (ingestThread.joinable()) {
        ingestThread.join();
    }
    if (motionThread.joinable()) {
        motionThread.join();
    }
    if (recorderThread.joinable()) {
        recorderThread.join();
    }
    if (!was && started.load()) {
        std::cout << "Pipeline stopped: motion-stage drops=" << motionQueue.dropCount()
                  << " record-stage drops=" << recordQueue.dropCount()
                  << " camera-queue drops=" << cameraQueueDrops()
                  << " pool-stalls=" << camera->poolStalls()
                  << " (expected whenever the sensor outpaces the sampled rate)" << std::endl;
    }
    runCv.notify_all();
}

void Pipeline::ingestLoop() {
    while (!stopRequested.load()) {
        SharedFrame frame = camera->captureImage();
        if (!frame) {
            if (stopRequested.load()) {
                break;
            }
            // Null also means the camera's frame pool was exhausted (a
            // consumer stalled past pool capacity; counted in poolStalls)
            // or a transient capture failure: back off, never allocate.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (stopRequested.load()) {
            break;
        }
        motionQueue.push(frame); // DropOldest: detection always sees the freshest.
        if (recordingActive.load()) {
            recordQueue.push(RecordEvent{frame, nullptr, false});
        }
        // Pace capture to the controller's cadence (idle fps until recording).
        // Excess sensor frames are dropped in the camera's own queue.
        std::this_thread::sleep_for(std::chrono::microseconds(captureIntervalUs.load()));
    }
}

void Pipeline::motionLoop() {
    bool wasRecording = false;
    while (true) {
        SharedFrame frame = motionQueue.pop();
        if (!frame) {
            break; // Closed and drained.
        }
        auto now = Clock::now();
        // The detector keeps the previous/current leases (pinned buffers);
        // the previous lease is released as the next frame arrives.
        detector.addFrame(std::move(frame));
        bool isRecording = controller.onFrame(now, detector.motionDetected());
        auto wait = controller.timeUntilNextCapture(now);
        captureIntervalUs.store(std::max<std::chrono::microseconds::rep>(
            wait.count(), 0));
        recordingActive.store(isRecording);

        if (isRecording && !wasRecording) {
            wasRecording = true;
            std::unique_ptr<VideoWriter> writer;
            try {
                writer = std::make_unique<VideoWriter>(options.videoDir, options.segments);
            } catch (const std::exception& e) {
                std::cerr << "Pipeline: could not start recorder: " << e.what() << std::endl;
                wasRecording = false;
                recordingActive.store(false);
                continue;
            }
            RecordEvent startEvent;
            startEvent.startWriter = std::move(writer);
            if (!recordQueue.push(std::move(startEvent))) {
                std::cerr << "Pipeline: recorder queue full at recording start" << std::endl;
            }
            if (options.driving.mode == OperatingMode::Driving) {
                std::cout << "Driving mode: starting continuous recording..." << std::endl;
            } else {
                std::cout << "Motion detected! Recording video..." << std::endl;
            }
        }
        // NOTE: frames are forwarded to the recorder by the ingest stage
        // (gated on recordingActive); this stage emits lifecycle events only.
        if (!isRecording && wasRecording) {
            wasRecording = false;
            RecordEvent stopEvent;
            stopEvent.stop = true;
            recordQueue.push(std::move(stopEvent));
            std::cout << "Done recording video." << std::endl;
            detector.reset();
        }
    }
}

void Pipeline::recorderLoop() {
    std::unique_ptr<VideoWriter> writer;
    while (true) {
        RecordEvent event = recordQueue.pop();
        if (!event.frame && !event.startWriter && !event.stop) {
            break; // Closed and drained.
        }
        if (event.startWriter) {
            writer = std::move(event.startWriter);
        }
        if (event.frame && writer) {
            writer->addFrame(std::move(event.frame));
        }
        if (event.stop) {
            // Runs the writer's flush-and-close here (blocking on the encoder
            // drain is fine on this thread; T1/T2 never wait on it).
            writer.reset();
        }
    }
    writer.reset();
}
