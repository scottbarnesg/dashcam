// Hardware H.264 encoder via the bcm2835 V4L2 M2M codec node.
//
// Structure and V4L2 sequencing adapted from rpicam-apps (BSD-2-Clause):
//   https://github.com/raspberrypi/rpicam-apps  encoder/h264_encoder.cpp
//   Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
// Differences: MM input buffers with memcpy (no DMABUF plumbing needed in
// the caller), per-control tolerance (unsupported controls are logged, not
// fatal), and a drain() counter handshake so segment closes flush cleanly.

#include "h264encoder.hpp"

#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>

namespace {

constexpr const char* kDevice = "/dev/video11";

int xioctl(int fd, unsigned long ctl, void* arg) {
    int ret, num_tries = 10;
    do {
        ret = ioctl(fd, ctl, arg);
    } while (ret == -1 && errno == EINTR && num_tries-- > 0);
    return ret;
}

int v4l2ColorSpace(ColorSpace cs) {
    switch (cs) {
        case ColorSpace::Rec709:
            return V4L2_COLORSPACE_REC709;
        case ColorSpace::Smpte170m:
            return V4L2_COLORSPACE_SMPTE170M;
        case ColorSpace::Unspecified:
            break;
    }
    return V4L2_COLORSPACE_SMPTE170M;
}

} // namespace

bool H264Encoder::available() {
    int probe = open(kDevice, O_RDWR | O_NONBLOCK, 0);
    if (probe < 0) {
        return false;
    }
    v4l2_capability caps = {};
    bool ok = xioctl(probe, VIDIOC_QUERYCAP, &caps) == 0 &&
              (caps.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) &&
              (caps.capabilities & V4L2_CAP_STREAMING);
    close(probe);
    return ok;
}

H264Encoder::H264Encoder(const Params& params) : settings(params) {
    if (settings.inputFormat != PixelFormat::NV12 && settings.inputFormat != PixelFormat::I420) {
        throw std::runtime_error("H264Encoder: input format must be NV12 or I420");
    }
    fd = open(kDevice, O_RDWR, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("H264Encoder: could not open ") + kDevice);
    }

    // Encoding controls. rpicam treats each as fatal; we log and continue so
    // an older/newer driver missing one control still records at defaults.
    auto setControl = [&](unsigned id, int value, const char* what) {
        v4l2_control ctrl = {};
        ctrl.id = id;
        ctrl.value = value;
        if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
            std::cerr << "H264Encoder: could not set " << what << " (non-fatal)" << std::endl;
        }
    };
    setControl(V4L2_CID_MPEG_VIDEO_BITRATE, settings.bitrateBps, "bitrate");
    setControl(V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_HIGH, "profile");
    setControl(V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_4_0, "level");
    setControl(V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, settings.gopFrames, "intra period");
    setControl(V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, 1, "inline sequence headers");

    v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.width = settings.width;
    fmt.fmt.pix_mp.height = settings.height;
    fmt.fmt.pix_mp.pixelformat =
        settings.inputFormat == PixelFormat::NV12 ? V4L2_PIX_FMT_NV12 : V4L2_PIX_FMT_YUV420;
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline = settings.width;
    fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
    fmt.fmt.pix_mp.colorspace = v4l2ColorSpace(settings.colorSpace);
    fmt.fmt.pix_mp.num_planes = 1;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        throw std::runtime_error("H264Encoder: failed to set output (input) format");
    }
    // The codec silently aligns bytesperline up (measured on the Pi:
    // 720 -> 736 NV12, 768 I420; widths that are already aligned come back
    // unchanged). Everything written to the input buffers must honour the
    // returned pitch; assuming width shears the picture diagonally and
    // misplaces the chroma planes (green/blue bands).
    inputStride = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    if (inputStride < static_cast<std::size_t>(settings.width)) {
        inputStride = static_cast<std::size_t>(settings.width);
    }
    if (fmt.fmt.pix_mp.width != static_cast<unsigned>(settings.width) ||
        fmt.fmt.pix_mp.height != static_cast<unsigned>(settings.height)) {
        throw std::runtime_error("H264Encoder: codec changed the requested frame size");
    }
    inputFrameBytes = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    if (inputFrameBytes < static_cast<std::size_t>(inputStride) * settings.height * 3 / 2) {
        inputFrameBytes = static_cast<std::size_t>(inputStride) * settings.height * 3 / 2;
    }

    fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = settings.width;
    fmt.fmt.pix_mp.height = settings.height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
    fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 512 << 10;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        throw std::runtime_error("H264Encoder: failed to set capture format");
    }

    if (settings.fps > 0) {
        v4l2_streamparm parm = {};
        parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        parm.parm.output.timeperframe.numerator = static_cast<unsigned>(90000.0 / settings.fps);
        parm.parm.output.timeperframe.denominator = 90000;
        if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
            std::cerr << "H264Encoder: could not set framerate (non-fatal)" << std::endl;
        }
    }

    // Input side: driver-allocated buffers we memcpy into (MM), so buffers
    // are ours to fill; the codec returns one each time it consumes a frame.
    v4l2_requestbuffers reqbufs = {};
    reqbufs.count = kNumInputBuffers;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &reqbufs) < 0) {
        throw std::runtime_error("H264Encoder: request for input buffers failed");
    }
    inputBuffers.resize(reqbufs.count);
    inputSizes.resize(reqbufs.count);
    for (unsigned int i = 0; i < reqbufs.count; i++) {
        v4l2_plane planes[VIDEO_MAX_PLANES] = {};
        v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        buffer.length = 1;
        buffer.m.planes = planes;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            throw std::runtime_error("H264Encoder: failed to query input buffer");
        }
        inputBuffers[i] = static_cast<unsigned char*>(
            mmap(nullptr, buffer.m.planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                 buffer.m.planes[0].m.mem_offset));
        if (inputBuffers[i] == MAP_FAILED) {
            throw std::runtime_error("H264Encoder: failed to mmap input buffer");
        }
        inputSizes[i] = buffer.m.planes[0].length;
        freeInputs.push(static_cast<int>(i)); // Start unqueued: we fill on demand.
    }

    // Output side: m-mapped buffers the codec writes the bitstream into;
    // queue them all up front ready for it to fill.
    reqbufs = {};
    reqbufs.count = kNumCaptureBuffers;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &reqbufs) < 0) {
        throw std::runtime_error("H264Encoder: request for capture buffers failed");
    }
    captureBuffers.resize(reqbufs.count);
    for (unsigned int i = 0; i < reqbufs.count; i++) {
        v4l2_plane planes[VIDEO_MAX_PLANES] = {};
        v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        buffer.length = 1;
        buffer.m.planes = planes;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            throw std::runtime_error("H264Encoder: failed to query capture buffer");
        }
        captureBuffers[i].mem = mmap(nullptr, buffer.m.planes[0].length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd, buffer.m.planes[0].m.mem_offset);
        if (captureBuffers[i].mem == MAP_FAILED) {
            throw std::runtime_error("H264Encoder: failed to mmap capture buffer");
        }
        captureBuffers[i].size = buffer.m.planes[0].length;
        if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            throw std::runtime_error("H264Encoder: failed to queue capture buffer");
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        throw std::runtime_error("H264Encoder: failed to start output streaming");
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        throw std::runtime_error("H264Encoder: failed to start capture streaming");
    }
    pollThreadHandle = std::thread(&H264Encoder::pollThread, this);
}

H264Encoder::~H264Encoder() {
    {
        std::lock_guard<std::mutex> lock(inputMutex);
        abortPoll = true;
    }
    inputAvailable.notify_all();
    if (pollThreadHandle.joinable()) {
        pollThreadHandle.join();
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        std::cerr << "H264Encoder: failed to stop output streaming" << std::endl;
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        std::cerr << "H264Encoder: failed to stop capture streaming" << std::endl;
    }

    v4l2_requestbuffers reqbufs = {};
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &reqbufs) < 0) {
        std::cerr << "H264Encoder: request to free input buffers failed" << std::endl;
    }
    for (std::size_t i = 0; i < inputBuffers.size(); i++) {
        if (inputBuffers[i] && inputBuffers[i] != MAP_FAILED) {
            munmap(inputBuffers[i], inputSizes[i]);
        }
    }
    reqbufs = {};
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &reqbufs) < 0) {
        std::cerr << "H264Encoder: request to free capture buffers failed" << std::endl;
    }
    for (const CaptureBuffer& buf : captureBuffers) {
        if (buf.mem && buf.mem != MAP_FAILED) {
            munmap(buf.mem, buf.size);
        }
    }
    if (fd >= 0) {
        close(fd);
    }
}

void H264Encoder::setOutputCallback(OutputCallback callback) {
    outputCallback = std::move(callback);
}

void H264Encoder::writeFrame(unsigned char* dst, const cv::Mat& yuv) const {
    const unsigned char* src = yuv.data;
    const std::size_t width = settings.width;
    const std::size_t height = settings.height;
    if (inputStride == width) {
        std::memcpy(dst, src, width * height * 3 / 2); // Packed layout: one copy.
        return;
    }
    for (std::size_t row = 0; row < height; row++) {
        std::memcpy(dst + row * inputStride, src + row * width, width);
    }
    unsigned char* dstChroma = dst + inputStride * height;
    const unsigned char* srcChroma = src + width * height;
    if (settings.inputFormat == PixelFormat::NV12) {
        for (std::size_t row = 0; row < height / 2; row++) {
            std::memcpy(dstChroma + row * inputStride, srcChroma + row * width, width);
        }
    } else {
        // I420 (single-plane convention): U at stride*H, V at stride*H +
        // (stride/2)*(H/2), chroma rows pitched stride/2.
        const std::size_t chromaWidth = width / 2;
        const std::size_t chromaStride = inputStride / 2;
        unsigned char* dstV = dstChroma + chromaStride * (height / 2);
        const unsigned char* srcV = srcChroma + chromaWidth * (height / 2);
        for (std::size_t row = 0; row < height / 2; row++) {
            std::memcpy(dstChroma + row * chromaStride, srcChroma + row * chromaWidth, chromaWidth);
            std::memcpy(dstV + row * chromaStride, srcV + row * chromaWidth, chromaWidth);
        }
    }
}

bool H264Encoder::encode(const cv::Mat& yuv, std::int64_t timestampUs,
                         std::chrono::milliseconds waitMs) {
    if (failed()) {
        return false;
    }
    if (yuv.cols != settings.width || yuv.rows != settings.height * 3 / 2) {
        setError("frame size does not match encoder configuration");
        return false;
    }
    int index = -1;
    {
        std::unique_lock<std::mutex> lock(inputMutex);
        if (abortPoll || error) {
            return false;
        }
        if (!inputAvailable.wait_for(lock, waitMs, [this] { return !freeInputs.empty() || abortPoll || error; }) ||
            freeInputs.empty()) {
            return false;
        }
        index = freeInputs.front();
        freeInputs.pop();
    }
    if (inputFrameBytes > inputSizes[index]) {
        setError("frame larger than encoder input buffer");
        std::lock_guard<std::mutex> lock(inputMutex);
        freeInputs.push(index);
        return false;
    }
    writeFrame(inputBuffers[index], yuv);

    v4l2_buffer buf = {};
    v4l2_plane planes[VIDEO_MAX_PLANES] = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.index = index;
    buf.field = V4L2_FIELD_NONE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = 1;
    buf.timestamp.tv_sec = timestampUs / 1000000;
    buf.timestamp.tv_usec = timestampUs % 1000000;
    buf.m.planes = planes;
    buf.m.planes[0].bytesused = inputFrameBytes;
    buf.m.planes[0].length = inputFrameBytes;
    if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        setError("failed to queue frame to codec");
        std::lock_guard<std::mutex> lock(inputMutex);
        freeInputs.push(index);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(doneMutex);
        queuedFrames++;
    }
    return true;
}

void H264Encoder::drain(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(doneMutex);
    doneCondition.wait_for(lock, timeout,
                           [this] { return deliveredFrames >= queuedFrames || error; });
}

bool H264Encoder::failed() const {
    std::lock_guard<std::mutex> lock(errorMutex);
    return error;
}

std::string H264Encoder::lastError() const {
    std::lock_guard<std::mutex> lock(errorMutex);
    return errorMessage;
}

void H264Encoder::setError(const std::string& message) {
    std::lock_guard<std::mutex> lock(errorMutex);
    if (!error) {
        error = true;
        errorMessage = message;
    }
    doneCondition.notify_all();
}

void H264Encoder::pollThread() {
    while (true) {
        pollfd p = { fd, POLLIN, 0 };
        int ret = poll(&p, 1, 200);
        {
            std::lock_guard<std::mutex> lock(inputMutex);
            if (abortPoll && freeInputs.size() == kNumInputBuffers) {
                break;
            }
        }
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            setError("unexpected errno from codec poll");
            break;
        }
        if (!(p.revents & POLLIN)) {
            continue;
        }

        // Consumed input frames: return those buffers to the free list.
        v4l2_buffer buf = {};
        v4l2_plane planes[VIDEO_MAX_PLANES] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = 1;
        buf.m.planes = planes;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) == 0) {
            std::lock_guard<std::mutex> lock(inputMutex);
            freeInputs.push(static_cast<int>(buf.index));
            inputAvailable.notify_one();
        }

        // Encoded bitstream: hand to the callback (muxer), then requeue.
        buf = {};
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = 1;
        buf.m.planes = planes;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) == 0) {
            bool keyframe = !!(buf.flags & V4L2_BUF_FLAG_KEYFRAME);
            std::int64_t tsUs = buf.timestamp.tv_sec * 1000000LL + buf.timestamp.tv_usec;
            if (outputCallback && !failed()) {
                try {
                    outputCallback(static_cast<const uint8_t*>(captureBuffers[buf.index].mem),
                                   buf.m.planes[0].bytesused, keyframe, tsUs);
                } catch (const std::exception& e) {
                    setError(std::string("output callback failed: ") + e.what());
                }
            }
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                deliveredFrames++;
                doneCondition.notify_all();
            }
            buf.m.planes[0].bytesused = 0;
            buf.m.planes[0].length = captureBuffers[buf.index].size;
            if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                setError("failed to requeue encoded buffer");
            }
        }
    }
}
