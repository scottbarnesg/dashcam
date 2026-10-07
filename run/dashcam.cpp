#include <chrono>
#include <iostream>
#include <thread>

#include "camera.hpp"
#include "driving.hpp"
#include "motion.hpp"
#include "video.hpp"

// Motion-as-driving proxy (BACKLOG item 2): motion starts recording; recording
// continues until no motion has been seen for the no-motion timeout N.
int main(int argc, char* argv[]) {
    std::string backend = "usb";
    if (argc > 1) {
        backend = argv[1];
    }
    auto camera = createCamera(backend);
    MotionDetector motionDetector = MotionDetector();
    DrivingController controller = DrivingController(DrivingController::Params{});

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
                    writer = std::make_unique<VideoWriter>("videos/");
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
