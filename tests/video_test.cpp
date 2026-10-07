#include "video.hpp"
#include "test.hpp"

#include <filesystem>
#include <opencv2/videoio.hpp>

namespace fs = std::filesystem;

int main() {
    fs::path dir = "test_videos";
    fs::remove_all(dir);
    std::vector<fs::path> filesBefore;

    {
        VideoWriter writer(dir);
        // 40 frames with a changing pattern, faster than the encoder can drain.
        for (int i = 0; i < 40; i++) {
            cv::Mat frame(120, 160, CV_8UC3, cv::Scalar(i * 5, 40, 90));
            writer.addFrame(frame);
        }
        // Empty frames are ignored, not written.
        writer.addFrame(cv::Mat());
    } // Destructor must flush, close the file, and join without hanging.

    fs::path produced;
    for (const auto& e : fs::directory_iterator(dir)) {
        produced = e.path();
    }
    CHECK(!produced.empty());
    CHECK(fs::file_size(produced) > 0);

    // Reopen with OpenCV: file must be valid (moov written) and contain frames.
    cv::VideoCapture cap(produced);
    CHECK(cap.isOpened());
    int frames = 0;
    cv::Mat frame;
    while (cap.read(frame)) {
        frames++;
    }
    CHECK(frames >= 30); // Some drops allowed under overload, but most must land.

    fs::remove_all(dir);
    TEST_RESULT();
}
