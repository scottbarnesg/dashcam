// Pi Camera Module capture backend using libcamera directly, implemented per
// the libcamera Application Writer's Guide (docs.libcamera.org). Requires
// libcamera-dev.
//
// Flow per the guide: CameraManager::start -> acquire camera ->
// generateConfiguration(VideoRecording) -> validate -> configure ->
// FrameBufferAllocator -> one Request per buffer -> start + queueRequest ->
// requestCompleted signal -> map planes, convert to BGR, requeue.

#include "camera.hpp"

#include <atomic>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include <libcamera/libcamera.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "queue.hpp"

// The project defines its own Camera interface; alias the libcamera types
// instead of pulling in the whole namespace.
using LCamera = libcamera::Camera;
using LCameraConfiguration = libcamera::CameraConfiguration;
using LCameraManager = libcamera::CameraManager;
using LFrameBuffer = libcamera::FrameBuffer;
using LFrameBufferAllocator = libcamera::FrameBufferAllocator;
using LFrameMetadata = libcamera::FrameMetadata;
using LPixelFormat = libcamera::PixelFormat;
using LRequest = libcamera::Request;
using LStream = libcamera::Stream;
using LStreamConfiguration = libcamera::StreamConfiguration;

class PiCamera::Impl {
    public:
        explicit Impl();
        ~Impl();

        void requestComplete(LRequest* request);

        SafeQueue<cv::Mat> frames{kQueueCapacity};
        std::atomic<bool> running{false};

    private:
        friend class PiCamera;

        static constexpr std::size_t kQueueCapacity = 8;
        static constexpr unsigned int kTargetWidth = 1280;
        static constexpr unsigned int kTargetHeight = 720;

        LCameraManager manager;
        std::shared_ptr<LCamera> camera;
        std::unique_ptr<LCameraConfiguration> config;
        std::unique_ptr<LFrameBufferAllocator> allocator;
        LStream* stream = nullptr;
        std::vector<std::unique_ptr<LRequest>> requests;

        LPixelFormat pixelFormat;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int stride = 0;

        void chooseCamera();
        void configureStream();
        void allocateRequests();
        cv::Mat convert(LFrameBuffer* buffer);
        cv::Mat convertNv12(const std::vector<LFrameBuffer::Plane>& planes);
        cv::Mat convertI420(const std::vector<LFrameBuffer::Plane>& planes);
        cv::Mat convertJpeg(const std::vector<LFrameBuffer::Plane>& planes, const LFrameMetadata& metadata);
        static void* mapPlane(const LFrameBuffer::Plane& plane);
        static void unmapPlane(void* address, const LFrameBuffer::Plane& plane);
};

PiCamera::PiCamera() : impl(std::make_unique<Impl>()) {
}

PiCamera::~PiCamera() = default;

cv::Mat PiCamera::captureImage() {
    return impl->frames.pop();
}

void PiCamera::Impl::requestComplete(LRequest* request) {
    // Runs on the libcamera event thread: convert, hand off, requeue; never block.
    if (request->status() != LRequest::RequestCancelled) {
        for (const auto& bufferPair : request->buffers()) {
            cv::Mat frame = convert(bufferPair.second);
            if (!frame.empty()) {
                frames.push(frame); // Drops when full; the camera never blocks.
            }
        }
    }
    if (running) {
        request->reuse(LRequest::ReuseBuffers);
        camera->queueRequest(request);
    }
}

PiCamera::Impl::Impl() {
    if (manager.start() < 0) {
        throw std::runtime_error("libcamera: could not start CameraManager");
    }
    try {
        chooseCamera();
        configureStream();
        allocateRequests();
    } catch (...) {
        if (camera) {
            camera->release();
            camera.reset();
        }
        manager.stop();
        throw;
    }

    camera->requestCompleted.connect(this, &PiCamera::Impl::requestComplete);
    running = true;
    if (camera->start() < 0) {
        running = false;
        throw std::runtime_error("libcamera: could not start camera");
    }
    for (std::unique_ptr<LRequest>& request : requests) {
        if (camera->queueRequest(request.get()) < 0) {
            running = false;
            throw std::runtime_error("libcamera: could not queue request");
        }
    }
    std::cout << "PiCamera: streaming " << width << "x" << height << " "
              << pixelFormat.toString() << std::endl;
}

PiCamera::Impl::~Impl() {
    // Order per the guide: stop camera, free buffers, release camera, stop manager.
    running = false;
    frames.close();
    if (camera) {
        camera->stop();
        if (allocator && stream) {
            allocator->free(stream);
        }
        allocator.reset();
        camera->release();
        camera.reset();
    }
    manager.stop();
}

void PiCamera::Impl::chooseCamera() {
    for (const std::shared_ptr<LCamera>& candidate : manager.cameras()) {
        std::shared_ptr<LCamera> found = manager.get(candidate->id());
        if (!found || found->acquire() < 0) {
            continue;
        }
        std::string model = found->properties().get(libcamera::properties::Model).value_or("unknown");
        std::unique_ptr<LCameraConfiguration> attempt =
            found->generateConfiguration({libcamera::StreamRole::VideoRecording});
        if (attempt && attempt->validate() != LCameraConfiguration::Invalid) {
            std::cout << "PiCamera: using camera '" << model << "' (" << candidate->id() << ")" << std::endl;
            camera = found;
            config = std::move(attempt);
            return;
        }
        found->release();
    }
    throw std::runtime_error("libcamera: no usable camera found (is it enabled in raspi-config?)");
}

void PiCamera::Impl::configureStream() {
    LStreamConfiguration& streamConfig = config->at(0);
    streamConfig.size.width = kTargetWidth;
    streamConfig.size.height = kTargetHeight;
    if (config->validate() == LCameraConfiguration::Invalid) {
        throw std::runtime_error("libcamera: requested stream configuration is not supported");
    }
    if (camera->configure(config.get()) < 0) {
        throw std::runtime_error("libcamera: could not apply stream configuration");
    }
    stream = streamConfig.stream();
    pixelFormat = streamConfig.pixelFormat;
    width = streamConfig.size.width;
    height = streamConfig.size.height;
    stride = streamConfig.stride;
    if (pixelFormat != libcamera::formats::NV12 && pixelFormat != libcamera::formats::YUV420 &&
        pixelFormat != libcamera::formats::MJPEG) {
        throw std::runtime_error("libcamera: unsupported pixel format " + pixelFormat.toString());
    }
}

void PiCamera::Impl::allocateRequests() {
    allocator = std::make_unique<LFrameBufferAllocator>(camera);
    if (allocator->allocate(stream) < 0) {
        throw std::runtime_error("libcamera: could not allocate frame buffers");
    }
    for (const std::unique_ptr<LFrameBuffer>& buffer : allocator->buffers(stream)) {
        std::unique_ptr<LRequest> request = camera->createRequest();
        if (!request) {
            throw std::runtime_error("libcamera: could not create request");
        }
        if (request->addBuffer(stream, buffer.get()) < 0) {
            throw std::runtime_error("libcamera: could not attach buffer to request");
        }
        requests.push_back(std::move(request));
    }
}

void* PiCamera::Impl::mapPlane(const LFrameBuffer::Plane& plane) {
    return mmap(nullptr, plane.length, PROT_READ, MAP_SHARED, plane.fd.get(), 0);
}

void PiCamera::Impl::unmapPlane(void* address, const LFrameBuffer::Plane& plane) {
    if (address != MAP_FAILED) {
        munmap(address, plane.length);
    }
}

cv::Mat PiCamera::Impl::convert(LFrameBuffer* buffer) {
    const LFrameMetadata& metadata = buffer->metadata();
    const std::vector<LFrameBuffer::Plane>& planes = buffer->planes();
    if (metadata.status != LFrameMetadata::FrameSuccess || planes.empty()) {
        return cv::Mat();
    }
    if (pixelFormat == libcamera::formats::MJPEG) {
        return convertJpeg(planes, metadata);
    }
    if (pixelFormat == libcamera::formats::NV12) {
        return convertNv12(planes);
    }
    return convertI420(planes);
}

cv::Mat PiCamera::Impl::convertNv12(const std::vector<LFrameBuffer::Plane>& planes) {
    if (planes.size() < 2) {
        return cv::Mat();
    }
    void* yMap = mapPlane(planes[0]);
    void* uvMap = mapPlane(planes[1]);
    cv::Mat result;
    if (yMap != MAP_FAILED && uvMap != MAP_FAILED) {
        unsigned int yStride = stride > 0 ? stride : width;
        unsigned int uvStride = planes[1].length / (height / 2);
        if (uvStride < width) {
            uvStride = width;
        }
        // Assemble a contiguous NV12 image (handles padded strides and
        // separately-allocated planes), then convert with OpenCV.
        cv::Mat yuv(height * 3 / 2, width, CV_8UC1);
        const unsigned char* y = static_cast<const unsigned char*>(yMap);
        const unsigned char* uv = static_cast<const unsigned char*>(uvMap);
        for (unsigned int row = 0; row < height; row++) {
            std::memcpy(yuv.row(row).ptr(), y + (std::size_t)row * yStride, width);
        }
        for (unsigned int row = 0; row < height / 2; row++) {
            std::memcpy(yuv.row(height + row).ptr(), uv + (std::size_t)row * uvStride, width);
        }
        cv::cvtColor(yuv, result, cv::COLOR_YUV2BGR_NV12);
    }
    unmapPlane(yMap, planes[0]);
    unmapPlane(uvMap, planes[1]);
    return result;
}

cv::Mat PiCamera::Impl::convertI420(const std::vector<LFrameBuffer::Plane>& planes) {
    if (planes.size() < 3) {
        return cv::Mat();
    }
    void* yMap = mapPlane(planes[0]);
    void* uMap = mapPlane(planes[1]);
    void* vMap = mapPlane(planes[2]);
    cv::Mat result;
    if (yMap != MAP_FAILED && uMap != MAP_FAILED && vMap != MAP_FAILED) {
        unsigned int yStride = stride > 0 ? stride : width;
        unsigned int cStride = yStride / 2;
        cv::Mat yuv(height * 3 / 2, width, CV_8UC1);
        const unsigned char* y = static_cast<const unsigned char*>(yMap);
        const unsigned char* u = static_cast<const unsigned char*>(uMap);
        const unsigned char* v = static_cast<const unsigned char*>(vMap);
        for (unsigned int row = 0; row < height; row++) {
            std::memcpy(yuv.row(row).ptr(), y + (std::size_t)row * yStride, width);
        }
        for (unsigned int row = 0; row < height / 2; row++) {
            std::memcpy(yuv.row(height + row).ptr(), u + (std::size_t)row * cStride, width / 2);
            std::memcpy(yuv.row(height + height / 2 + row).ptr(), v + (std::size_t)row * cStride, width / 2);
        }
        cv::cvtColor(yuv, result, cv::COLOR_YUV2BGR_I420);
    }
    unmapPlane(yMap, planes[0]);
    unmapPlane(uMap, planes[1]);
    unmapPlane(vMap, planes[2]);
    return result;
}

cv::Mat PiCamera::Impl::convertJpeg(const std::vector<LFrameBuffer::Plane>& planes,
                                    const LFrameMetadata& metadata) {
    void* map = mapPlane(planes[0]);
    cv::Mat result;
    if (map != MAP_FAILED) {
        auto span = metadata.planes();
        std::size_t bytes = span.empty() ? planes[0].length : span[0].bytesused;
        bytes = std::min(bytes, static_cast<std::size_t>(planes[0].length));
        const unsigned char* begin = static_cast<const unsigned char*>(map);
        std::vector<unsigned char> encoded(begin, begin + bytes);
        result = cv::imdecode(encoded, cv::IMREAD_COLOR);
    }
    unmapPlane(map, planes[0]);
    return result;
}

