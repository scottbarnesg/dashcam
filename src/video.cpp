#include "video.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "naming.hpp"

namespace {

std::int64_t epochUs(std::chrono::system_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}

// Capture time when the frame carries one, wall time otherwise (BGR/dev
// inputs). Segment boundaries and PTS follow the capture clock so segment
// lengths and playback speed track real time even when a stage stalls.
std::chrono::system_clock::time_point frameTime(const RawFrame& frame) {
    return frame.timestamp == std::chrono::system_clock::time_point{} ? std::chrono::system_clock::now()
                                                                      : frame.timestamp;
}

// YUV (packed, per frame.hpp) -> BGR for the software encoder path.
void toBgr(const RawFrame& frame, cv::Mat& out) {
    switch (frame.format) {
        case PixelFormat::NV12:
            cv::cvtColor(frame.data, out, cv::COLOR_YUV2BGR_NV12);
            break;
        case PixelFormat::I420:
            cv::cvtColor(frame.data, out, cv::COLOR_YUV2BGR_I420);
            break;
        case PixelFormat::BGR:
            break;
    }
}

} // namespace

VideoWriter::VideoWriter(std::filesystem::path fileDir, SegmentOptions opts)
    : outputDir(std::move(fileDir)), options(std::move(opts)) {
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        std::cerr << "VideoWriter: could not create output directory " << outputDir << std::endl;
    }
    writeThread = std::thread(&VideoWriter::writeToFile, this);
}

VideoWriter::~VideoWriter() {
    frameBuffer.close();
    if (writeThread.joinable()) {
        writeThread.join();
    }
    if (droppedFrameCount.load() > 0) {
        std::cerr << "VideoWriter dropped " << droppedFrameCount.load() << " frames (encoder fell behind)" << std::endl;
    }
}

void VideoWriter::addFrame(RawFrame frame) {
    if (frame.empty()) {
        return;
    }
    std::scoped_lock lock(frameBufferMut);
    if (frameBuffer.empty()) {
        firstFrameTime = getCurrentTime();
    }
    lastFrameTime = getCurrentTime();
    framesSeen++;
    if (!frameBuffer.push(std::move(frame))) {
        droppedFrameCount++;
    }
}

std::chrono::time_point<std::chrono::system_clock> VideoWriter::getCurrentTime() {
    return std::chrono::system_clock::now();
}

int VideoWriter::calculateFPS() {
    std::chrono::duration<float> elapsed = lastFrameTime - firstFrameTime;
    int fps = 1;
    if (elapsed > std::chrono::milliseconds(100) && framesSeen >= 2) {
        fps = (framesSeen - 1) / elapsed.count();
    }
    return std::clamp(fps, 1, 120);
}

int VideoWriter::segmentSequence() const {
    unsigned long seq = 1;
    if (options.manifest) {
        for (const ManifestEntry& e : options.manifest->entries()) {
            auto pos = e.file.find_last_of('_');
            if (pos == std::string::npos) {
                continue;
            }
            try {
                unsigned long s = std::stoul(e.file.substr(pos + 1, 4));
                seq = std::max(seq, s + 1);
            } catch (const std::exception&) {
                // Not one of ours; ignore.
            }
        }
    }
    return static_cast<int>(seq);
}

bool VideoWriter::hasFreeSpace() {
    std::error_code ec;
    auto space = std::filesystem::space(outputDir, ec);
    if (ec) {
        // Can't stat the volume; assume OK and let the write fail if it must.
        return true;
    }
    return space.available >= options.minFreeBytes;
}

void VideoWriter::openSegment(std::chrono::system_clock::time_point frameTime, const RawFrame& firstFrame) {
    if (!hasFreeSpace()) {
        std::cerr << "VideoWriter: out of disk space, segment not started" << std::endl;
        return;
    }
    std::string name = naming::fileName(frameTime, options.context, segmentSequence());
    segmentPath = outputDir / name;
    sentinelPath = segmentPath;
    sentinelPath += ".writing";
    std::ofstream(sentinelPath).close(); // Marker: file exists but is not yet closed.
    // Prefer the configured recording rate for playback timing: estimating from
    // frame arrival is wrong for the first segment, whose warm-up window straddles
    // the slow idle->recording transition and yields a spurious ~1 fps.
    int fps = options.recordingFps > 0 ? options.recordingFps : calculateFPS();
    cv::Size size = firstFrame.size();

    // Hardware path: V4L2 M2M H.264 codec -> fragmented MP4. One encoder per
    // segment guarantees the segment opens with an IDR.
    bool wantHw = options.encoder == "hw" || (options.encoder == "auto" && !hwUnavailable);
    usingHw = false;
    if (wantHw) {
        try {
            H264Encoder::Params params;
            params.width = size.width;
            params.height = size.height;
            params.fps = fps;
            params.bitrateBps = options.bitrateKbps * 1000;
            params.gopFrames = std::max(1, fps * options.gopSeconds);
            params.inputFormat = firstFrame.format == PixelFormat::BGR ? PixelFormat::I420 : firstFrame.format;
            params.colorSpace = firstFrame.colorSpace;
            muxer = std::make_unique<Mp4Muxer>(segmentPath, size.width, size.height, fps);
            muxer->open();
            encoder = std::make_unique<H264Encoder>(params);
            Mp4Muxer* m = muxer.get();
            encoder->setOutputCallback([m](const uint8_t* data, std::size_t size_, bool keyframe, std::int64_t captureUs) {
                m->write(data, size_, keyframe, captureUs);
            });
            usingHw = true;
        } catch (const std::exception& e) {
            std::cerr << "VideoWriter: hardware encoder unavailable (" << e.what()
                      << "), falling back to software" << std::endl;
            if (options.encoder == "auto") {
                hwUnavailable = true;
            }
            encoder.reset();
            muxer.reset();
            usingHw = false;
        }
    }

    if (usingHw) {
        std::cout << "VideoWriter: opening segment " << name << " at " << fps
                  << " fps (hardware H.264)" << std::endl;
    } else {
        std::cout << "VideoWriter: opening segment " << name << " at " << fps << " fps" << std::endl;
        int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        segmentWriter = cv::VideoWriter(segmentPath, fourcc, fps, size);
        if (!segmentWriter.isOpened()) {
            std::cerr << "VideoWriter: could not open output file " << segmentPath << std::endl;
        }
    }
    segmentStart = frameTime;
    segmentFrames = 0;
}

void VideoWriter::closeSegment() {
    if (usingHw) {
        // Drain first, then join the encoder's callback thread (via its
        // destructor), and only then touch the muxer: after the join no
        // callback can be in flight, so close() has sole ownership.
        encoder->drain(std::chrono::seconds(2));
        if (encoder->failed()) {
            std::cerr << "VideoWriter: encoder error: " << encoder->lastError() << std::endl;
        }
        encoder.reset();
        muxer->close();
        if (!muxer->ok()) {
            std::cerr << "VideoWriter: muxer error: " << muxer->lastError() << std::endl;
        }
        muxer.reset();
        usingHw = false;
    } else if (segmentWriter.isOpened()) {
        segmentWriter.release();
    } else {
        std::filesystem::remove(sentinelPath); // Segment never opened (disk full / bad codec).
        return;
    }
    std::filesystem::remove(sentinelPath);
    _segmentsWritten++;
    // Per-segment telemetry (BACKLOG item 2 asked for fps logging): the encode
    // rate vs the configured rate and the running drop count tell capture-side
    // slowness apart from encoder-side drops.
    double elapsed = std::chrono::duration<double>(getCurrentTime() - segmentStart).count();
    double encodeFps = elapsed > 0 ? segmentFrames / elapsed : 0.0;
    std::cout << "VideoWriter: closed " << segmentPath.filename().string() << ": " << segmentFrames
              << " frames, " << std::fixed << std::setprecision(1) << encodeFps
              << " fps, dropped so far: " << droppedFrameCount.load() << std::endl;
    if (options.manifest) {
        std::error_code ec;
        auto bytes = std::filesystem::file_size(segmentPath, ec);
        if (ec || bytes == 0) {
            std::cerr << "VideoWriter: segment " << segmentPath << " is empty; not registered" << std::endl;
        } else {
            double duration = segmentFrames > 1
                ? std::chrono::duration<double>(getCurrentTime() - segmentStart).count()
                : 0.0;
            options.manifest->addRecording(segmentPath, options.context, naming::timestamp(segmentStart), duration);
            if (!options.manifest->save()) {
                std::cerr << "VideoWriter: failed to persist manifest" << std::endl;
            }
        }
    }
}

void VideoWriter::submitFrame(RawFrame& frame) {
    if (usingHw) {
        cv::Mat yuv;
        const cv::Mat* data = &frame.data;
        if (frame.format == PixelFormat::BGR) {
            // The hardware path was configured for I420 when the segment
            // started on BGR frames (COLOR_BGR2YUV_I420 is available on
            // older OpenCV; the NV12 conversion is not).
            cv::cvtColor(frame.data, yuv, cv::COLOR_BGR2YUV_I420);
            data = &yuv;
        }
        if (encoder->encode(*data, epochUs(frameTime(frame)))) {
            segmentFrames++;
        } else {
            droppedFrameCount++;
        }
        return;
    }
    if (!segmentWriter.isOpened()) {
        return;
    }
    cv::Mat bgr;
    toBgr(frame, bgr);
    segmentWriter.write(bgr.empty() ? frame.data : bgr);
    segmentFrames++;
}

void VideoWriter::writeToFile() {
    // Warm up: hold a few frames so the FPS estimate is meaningful before opening the file.
    std::vector<RawFrame> warmup;
    while (warmup.size() < 10) {
        RawFrame frame = frameBuffer.pop();
        if (frame.empty()) {
            return; // Buffer closed before any video was produced.
        }
        warmup.push_back(std::move(frame));
    }
    openSegment(frameTime(warmup.front()), warmup.front());
    for (RawFrame& frame : warmup) {
        submitFrame(frame);
    }
    while (true) {
        RawFrame frame = frameBuffer.pop();
        if (frame.empty()) {
            break; // Buffer closed and drained.
        }
        bool segmentActive = usingHw || segmentWriter.isOpened();
        auto stamp = frameTime(frame);
        if (segmentActive && stamp - segmentStart >= std::chrono::seconds(options.lengthSeconds)) {
            closeSegment(); // Self-closing segment: durable across power loss.
            openSegment(stamp, frame);
        }
        if (usingHw && encoder->failed()) {
            // Encode-side failure (e.g. codec node died): roll the segment so
            // the error surfaces and a fresh encoder (or the software path)
            // takes over.
            std::cerr << "VideoWriter: rolling segment after encoder failure" << std::endl;
            closeSegment();
            openSegment(getCurrentTime(), frame);
        }
        if (usingHw || segmentWriter.isOpened()) {
            submitFrame(frame);
        }
    }
    closeSegment();
}
