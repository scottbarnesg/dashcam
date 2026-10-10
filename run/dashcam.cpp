#include <iostream>

#include "camera.hpp"
#include "config.hpp"
#include "manifest.hpp"
#include "pipeline.hpp"
#include "recovery.hpp"

// Motion-as-driving-proxy state machine (BACKLOG item 2), now staged across
// three threads (pipeline.hpp): capture ingest, motion/control, and
// recording, so slow motion analysis can never starve the recorder or block
// frame intake.
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

    auto camera = createCamera(config.cameraBackend, config.cameraOrientation);
    std::string backend = camera->name();

    Pipeline::Options options;
    options.motionThreshold = config.motionThreshold;
    options.driving.noMotionTimeout = std::chrono::seconds(config.noMotionTimeoutSeconds);
    options.driving.idleFps = config.idleFps;
    options.driving.recordingFps = config.recordingFps;
    options.videoDir = config.videoDir;
    options.segments.lengthSeconds = config.segmentLengthSeconds;
    options.segments.recordingFps = static_cast<int>(config.recordingFps);
    options.segments.context = "seg";
    options.segments.manifest = &manifest;
    options.segments.encoder = config.encoder;
    options.segments.bitrateKbps = config.videoBitrateKbps;
    options.segments.gopSeconds = config.videoGopSeconds;

    Pipeline pipeline(std::move(camera), options);
    std::cout << "Dashcam started with camera backend: " << backend << std::endl;
    pipeline.run(); // Blocks until interrupted (SIGINT terminates, as before).
    return 0;
}
