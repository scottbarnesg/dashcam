#include <vector>

#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>

#ifndef CAMERA_H
#define CAMERA_H

class USBCamera {
    public:
        USBCamera();
        ~USBCamera();
        cv::Mat captureImage();
    private:
       std::vector<uchar> encodeImage(cv::Mat image); // TODO: Figure out how to use this.
       cv::VideoCapture cam;
       int cameraIndex = 0;
       const std::vector<int> encodeParams = {cv::IMWRITE_JPEG_QUALITY, 95};
};

#endif