#include "camera.hpp"
#include "test.hpp"

#include <stdexcept>

int main() {
    // Factory
    CHECK_EQ(createCamera("sim")->name(), std::string("sim"));
    bool threw = false;
    try {
        createCamera("nonexistent");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // Simulated camera produces frames
    auto cam = createCamera("sim");
    cv::Mat frame = cam->captureImage();
    CHECK(!frame.empty());
    CHECK_EQ(frame.channels(), 3);

    // Pi backend only registered when built with USE_PI_CAMERA
#ifdef USE_PI_CAMERA
    CHECK_EQ(createCamera("pi")->name(), std::string("pi"));
#else
    threw = false;
    try {
        createCamera("pi");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
#endif

    TEST_RESULT();
}
