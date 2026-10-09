#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

#include "frame.hpp"

#ifndef H264ENCODER_HPP
#define H264ENCODER_HPP

// Hardware H.264 encoder for the Raspberry Pi's V4L2 M2M codec
// (/dev/video11, "bcm2835-codec-encode"). Adapted from rpicam-apps'
// h264_encoder.cpp (BSD-2-Clause, Copyright (C) 2020, Raspberry Pi
// (Trading) Ltd.); the key change is that input arrives as ordinary
// cv::Mats that are memcpy'd into driver-allocated (MM) buffers instead
// of caller-supplied DMABUFs, so the capture path needs no dmabuf
// plumbing. One instance encodes one segment: constructed per segment
// (which guarantees the segment starts with an IDR), destroyed on close.
//
// Thread model: an internal poll thread waits for codec events; for each
// encoded access unit it invokes the output callback (Annex-B H.264) on
// that thread, so the callback (an MP4 muxer write) must be quick and
// must not throw. encode()/drain() run on the caller's thread.
class H264Encoder {
    public:
        struct Params {
            int width = 0;
            int height = 0;
            int fps = 30;
            int bitrateBps = 5000000;
            int gopFrames = 60;                    // IDR interval (V4L2 I_PERIOD).
            PixelFormat inputFormat = PixelFormat::NV12; // NV12 or I420 only.
            ColorSpace colorSpace = ColorSpace::Unspecified;
        };

        // Callback signature: (data, bytes, keyframe, capture timestamp us).
        using OutputCallback =
            std::function<void(const uint8_t*, std::size_t, bool, std::int64_t)>;

        // Probes the codec node without claiming it.
        static bool available();

        // Throws std::runtime_error if the codec cannot be opened/configured.
        explicit H264Encoder(const Params& params);
        ~H264Encoder();

        H264Encoder(const H264Encoder&) = delete;
        H264Encoder& operator=(const H264Encoder&) = delete;

        void setOutputCallback(OutputCallback callback);

        // Copy a packed YUV frame (CV_8UC1, 3H/2 rows) into a free input
        // buffer and queue it for encoding. Waits up to waitMs for a buffer
        // to free up; returns false if none did (caller should count a drop).
        bool encode(const cv::Mat& yuv, std::int64_t timestampUs,
                    std::chrono::milliseconds waitMs = std::chrono::milliseconds(150));

        // Block until every frame passed to encode() has been emitted through
        // the output callback (or timeout). Call before destruction so no
        // trailing frames are lost.
        void drain(std::chrono::milliseconds timeout);

        // True once the poll thread or the output callback hit an error;
        // the encoder must be torn down and rebuilt (new segment).
        bool failed() const;
        std::string lastError() const;

    private:
        void pollThread();

        static constexpr int kNumInputBuffers = 6;   // codec inputs (we own copies)
        static constexpr int kNumCaptureBuffers = 12; // encoded bitstream buffers

        int fd = -1;
        Params settings;
        OutputCallback outputCallback;

        struct CaptureBuffer {
            void* mem = nullptr;
            std::size_t size = 0;
        };
        std::vector<CaptureBuffer> captureBuffers;
        std::vector<unsigned char*> inputBuffers;
        std::vector<std::size_t> inputSizes;
        std::mutex inputMutex;
        std::condition_variable inputAvailable;
        std::queue<int> freeInputs;

        std::thread pollThreadHandle;
        bool abortPoll = false;

        std::mutex doneMutex;
        std::condition_variable doneCondition;
        long queuedFrames = 0;
        long deliveredFrames = 0;

        mutable std::mutex errorMutex;
        bool error = false;
        std::string errorMessage;
        void setError(const std::string& message);
};

#endif
