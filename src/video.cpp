#include "video.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "naming.hpp"

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
    if (dropped_.load() > 0) {
        std::cerr << "VideoWriter dropped " << dropped_.load() << " frames (encoder fell behind)" << std::endl;
    }
}

void VideoWriter::addFrame(cv::Mat frame) {
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
        dropped_++;
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

void VideoWriter::openSegment(std::chrono::system_clock::time_point frameTime, cv::Size frameSize) {
    if (!hasFreeSpace()) {
        std::cerr << "VideoWriter: out of disk space, segment not started" << std::endl;
        return;
    }
    std::string name = naming::fileName(frameTime, options.context, segmentSequence());
    segmentPath = outputDir / name;
    sentinelPath = segmentPath;
    sentinelPath += ".writing";
    std::ofstream(sentinelPath).close(); // Marker: file exists but is not yet closed.
    int fps = calculateFPS();
    std::cout << "VideoWriter: opening segment " << name << " at " << fps << " fps" << std::endl;
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    segmentWriter = cv::VideoWriter(segmentPath, fourcc, fps, frameSize);
    segmentStart = frameTime;
    segmentFrames = 0;
    if (!segmentWriter.isOpened()) {
        std::cerr << "VideoWriter: could not open output file " << segmentPath << std::endl;
    }
}

void VideoWriter::closeSegment() {
    if (!segmentWriter.isOpened()) {
        std::filesystem::remove(sentinelPath);
        return;
    }
    segmentWriter.release();
    std::filesystem::remove(sentinelPath);
    segmentsWritten_++;
    if (options.manifest) {
        double duration = segmentFrames > 1
            ? std::chrono::duration<double>(getCurrentTime() - segmentStart).count()
            : 0.0;
        options.manifest->addRecording(segmentPath, options.context, naming::timestamp(segmentStart), duration);
        if (!options.manifest->save()) {
            std::cerr << "VideoWriter: failed to persist manifest" << std::endl;
        }
    }
}

void VideoWriter::writeToFile() {
    // Warm up: hold a few frames so the FPS estimate is meaningful before opening the file.
    std::vector<cv::Mat> warmup;
    while (warmup.size() < 10) {
        cv::Mat frame = frameBuffer.pop();
        if (frame.empty()) {
            return; // Buffer closed before any video was produced.
        }
        warmup.push_back(std::move(frame));
    }
    openSegment(getCurrentTime(), warmup.front().size());
    for (cv::Mat& frame : warmup) {
        if (segmentWriter.isOpened()) {
            segmentWriter.write(frame);
            segmentFrames++;
        }
    }
    while (true) {
        cv::Mat frame = frameBuffer.pop();
        if (frame.empty()) {
            break; // Buffer closed and drained.
        }
        auto frameTime = getCurrentTime();
        if (segmentWriter.isOpened() &&
            std::chrono::duration_cast<std::chrono::seconds>(frameTime - segmentStart).count() >= options.lengthSeconds) {
            closeSegment(); // Self-closing segment: durable across power loss.
            openSegment(frameTime, frame.size());
        }
        if (segmentWriter.isOpened()) {
            segmentWriter.write(frame);
            segmentFrames++;
        }
    }
    closeSegment();
}
