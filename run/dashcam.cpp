#include <chrono>
#include <iostream>
#include <thread>

#include "camera.hpp"
#include "config.hpp"
#include "driving.hpp"
#include "manifest.hpp"
#include "motion.hpp"
#include "recovery.hpp"
#include "video.hpp"

// Motion-as-driving proxy (BACKLOG item 2): motion starts recording; recording
// continues until no motion has been seen for the no-motion timeout N.
int main(int argc, char* argv[]) {
    std::filesystem::path configPath = argc > 1 ? argv[1] : "dashcam.conf";
    Config config = Config::load(configPath);

    // Boot recovery (item 3): quarantine segments left open by a power cut,
    // then rebuild manifest entries for any completed file not yet registered.
    quarantineIncompleteSegments(config.videoDir);
    Manifest manifest(config.videoDir);
    manifest.load();
    std::size_t recovered = manifest.rebuildFromDisk();
    if (recovered > 0) {
        std::cout << "Recovery: registered " << recovered << " unregistered recording(s) in the manifest" << std::endl;
    }

    auto camera = createCamera(config.cameraBackend);
    MotionDetector motionDetector = MotionDetector(config.motionThreshold);
    DrivingController::Params params;
    params.noMotionTimeout = std::chrono::seconds(config.noMotionTimeoutSeconds);
    params.idleFps = config.idleFps;
    params.recordingFps = config.recordingFps;
    DrivingController controller = DrivingController(params);

    std::cout << "Dashcam started with camera backend: " << camera->name() << std::endl;

    std::unique_ptr<VideoWriter> writer;
    while (true) {
        auto now = std::chrono::system_clock::now();
        auto frame = camera->captureImage();
        if (frame.empty()) {
            std::cerr << "Warning: capture returned no frame, skipping" << std::endl;
        } else {
            motionDetector.addFrame(frame);
            bool recording = controller.onFrame(now, motionDetector.motionDetected());
            if (recording) {
                if (!writer) {
                    std::cout << "Motion detected! Recording video..." << std::endl;
                    VideoWriter::SegmentOptions seg;
                    seg.lengthSeconds = config.segmentLengthSeconds;
                    seg.recordingFps = static_cast<int>(config.recordingFps);
                    seg.context = "seg";
                    seg.manifest = &manifest;
                    writer = std::make_unique<VideoWriter>(config.videoDir, seg);
                }
                writer->addFrame(frame);
            } else if (writer) {
                writer.reset(); // Destructor flushes buffered frames and closes the file.
                motionDetector.reset();
                std::cout << "Done recording video." << std::endl;
            }
        }
        std::this_thread::sleep_for(controller.timeUntilNextCapture(std::chrono::system_clock::now()));
    }
    return 0;
}
