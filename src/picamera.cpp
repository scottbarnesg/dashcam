// Pi Camera Module capture backend using libcamera directly, implemented per
// the libcamera Application Writer's Guide (docs.libcamera.org). Requires
// libcamera-dev.
//
// Flow per the guide: CameraManager::start -> acquire camera ->
// generateConfiguration(VideoRecording) -> validate -> configure ->
// FrameBufferAllocator -> one Request per buffer -> start + queueRequest ->
// requestCompleted signal -> map planes, emit native NV12/I420 RawFrame,
// requeue. No BGR conversion: motion detection uses the Y plane and the
// hardware encoder consumes NV12 directly.

#include "camera.hpp"

#include "orientation.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
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
        explicit Impl(CameraOrientation requestedOrientation);
        ~Impl();

        // Idempotent shutdown (see definition): safe from any thread, runs at
        // most once (first caller wins).
        void teardown();

        void requestComplete(LRequest* request);

        // DropOldest: when the pipeline samples below sensor rate (idle fps,
        // or a transient consumer stall) the freshest frames must survive —
        // DropNewest would freeze the queue contents and deliver a lagging,
        // timestamp-compressed view of reality.
        SafeQueue<RawFrame> frames{kQueueCapacity, SafeQueue<RawFrame>::Overflow::DropOldest};
        std::atomic<bool> running{false};
        std::atomic_flag tornDown = ATOMIC_FLAG_INIT;

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
        std::optional<libcamera::ColorSpace> streamColorSpace;
        ColorSpace frameColorSpace = ColorSpace::Unspecified;
        // Item 7: per-request correction (Auto = follow the sensor) and the
        // resolved transform applied to every emitted frame in convert().
        CameraOrientation requestedOrientation = CameraOrientation::Auto;
        CameraOrientation orientation = CameraOrientation::Rotate0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int stride = 0;
        bool loggedPlanes = false;
        bool orientationWarned = false;

        void chooseCamera();
        // Item 7: resolve the frame correction. An explicit config value
        // wins; "auto" follows the sensor's reported Rotation property
        // (libcamera's cross-version camera property: counter-clockwise
        // degrees; current libcamera has no Orientation *property*, only
        // this one), falling back to no correction when it reports nothing.
        void resolveOrientation();
        void configureStream();
        void allocateRequests();
        // Debug aid: PICAMERA_DEBUG_DUMP=N writes the first N assembled raw
        // frames to /tmp/picamera_dump_NNN.yuv (contiguous format-native layout)
        // and logs per-plane lengths. Used to diagnose conversion artifacts.
        void dumpFrame(const cv::Mat& yuv, const char* fmt);
        unsigned int dumpCount = 0;
        unsigned int dumpLimit = 0xFFFFFFFF;
        bool dumpChecked = false;
        unsigned int dumpIndex = 0;
        RawFrame convert(LFrameBuffer* buffer);
        // Templated on the plane container: std::vector<Plane> on libcamera
        // 0.2 (Bookworm's older releases), Span<const Plane> on 0.3+.
        template <typename Planes>
        cv::Mat convertNv12(const Planes& planes);
        template <typename Planes>
        cv::Mat convertI420(const Planes& planes);
        template <typename Planes>
        cv::Mat convertJpeg(const Planes& planes, const LFrameMetadata& metadata);
        static void* mapPlane(const LFrameBuffer::Plane& plane);
        static const unsigned char* planeData(void* address, const LFrameBuffer::Plane& plane);
        static void unmapPlane(void* address, const LFrameBuffer::Plane& plane);
};

PiCamera::PiCamera(CameraOrientation requestedOrientation)
    : impl(std::make_unique<Impl>(requestedOrientation)) {
}

PiCamera::~PiCamera() = default;

RawFrame PiCamera::captureImage() {
    return impl->frames.pop();
}

void PiCamera::stop() {
    impl->teardown();
}

std::size_t PiCamera::queueDrops() const {
    return impl->frames.dropCount();
}

void PiCamera::Impl::requestComplete(LRequest* request) {
    // Runs on the libcamera event thread: assemble, hand off, requeue; never block.
    if (request->status() != LRequest::RequestCancelled) {
        for (const auto& bufferPair : request->buffers()) {
            RawFrame frame = convert(bufferPair.second);
            if (!frame.empty()) {
                frames.push(std::move(frame)); // Drops when full; the camera never blocks.
            }
        }
    }
    if (running) {
        request->reuse(LRequest::ReuseBuffers);
        camera->queueRequest(request);
    }
}

PiCamera::Impl::Impl(CameraOrientation requested) {
    requestedOrientation = requested;
    if (manager.start() < 0) {
        throw std::runtime_error("libcamera: could not start CameraManager");
    }
    try {
        chooseCamera();
        resolveOrientation();
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
    teardown();
}

void PiCamera::Impl::teardown() {
    // Idempotent: PiCamera::stop() and the destructor may both run (stop
    // unblocks a captureImage that is parked in pop(); the destructor then
    // finds nothing left to do).
    if (tornDown.test_and_set()) {
        return;
    }
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
        auto model = found->properties().get(libcamera::properties::Model);
        std::string modelString = model ? std::string(model->data(), model->size()) : "unknown";
        std::unique_ptr<LCameraConfiguration> attempt =
            found->generateConfiguration({libcamera::StreamRole::VideoRecording});
        if (attempt && attempt->validate() != LCameraConfiguration::Invalid) {
            std::cout << "PiCamera: using camera '" << modelString << "' (" << candidate->id() << ")" << std::endl;
            camera = found;
            config = std::move(attempt);
            return;
        }
        found->release();
    }
    throw std::runtime_error("libcamera: no usable camera found (is it enabled in raspi-config?)");
}

void PiCamera::Impl::resolveOrientation() {
    if (requestedOrientation != CameraOrientation::Auto) {
        orientation = requestedOrientation;
        std::cout << "PiCamera: using configured orientation override" << std::endl;
        return;
    }
    auto rotation = camera->properties().get(libcamera::properties::Rotation);
    if (rotation) {
        orientation = orientationFromRotationProperty(*rotation);
        std::cout << "PiCamera: sensor reports rotation " << *rotation << " deg (CCW)" << std::endl;
    } else {
        orientation = CameraOrientation::Rotate0;
        std::cout << "PiCamera: sensor reports no orientation; frames pass through "
                  << "unchanged (set camera_orientation if playback is rotated)" << std::endl;
    }
}

void PiCamera::Impl::configureStream() {
    LStreamConfiguration& streamConfig = config->at(0);
    streamConfig.size.width = kTargetWidth;
    streamConfig.size.height = kTargetHeight;
    // Request semi-planar NV12 explicitly. On the Pi the ISP delivers NV12
    // even when the stream negotiates planar YUV420, which the planar
    // converter then reads as garbage chroma (green bands). rpicam-apps
    // always uses NV12 for the same reason.
    streamConfig.pixelFormat = libcamera::formats::NV12;
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
    streamColorSpace = streamConfig.colorSpace;
    if (streamColorSpace) {
        if (*streamColorSpace == libcamera::ColorSpace::Rec709) {
            frameColorSpace = ColorSpace::Rec709;
        } else if (*streamColorSpace == libcamera::ColorSpace::Smpte170m) {
            frameColorSpace = ColorSpace::Smpte170m;
        }
    }
    std::cout << "PiCamera: negotiated " << width << "x" << height
              << " format=" << pixelFormat.toString()
              << " stride=" << stride
              << " frameSize=" << streamConfig.frameSize << std::endl;
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

void PiCamera::Impl::dumpFrame(const cv::Mat& yuv, const char* fmt) {
    if (!dumpChecked) {
        dumpChecked = true;
        const char* env = std::getenv("PICAMERA_DEBUG_DUMP");
        dumpLimit = env ? static_cast<unsigned int>(std::atoi(env)) : 0;
    }
    if (dumpCount >= dumpLimit) {
        return;
    }
    char path[64];
    std::snprintf(path, sizeof(path), "/tmp/picamera_dump_%03u.yuv", dumpIndex++);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(yuv.data), yuv.total());
    dumpCount++;
    (void)fmt;
}

void* PiCamera::Impl::mapPlane(const LFrameBuffer::Plane& plane) {
    // Planes may be sub-buffers of one shared allocation (observed on the Pi:
    // U and V share luma's buffer at non-zero offsets), so the mapping must
    // span plane.offset + plane.length; the data itself starts at offset.
    return mmap(nullptr, plane.offset + plane.length, PROT_READ, MAP_SHARED, plane.fd.get(), 0);
}

const unsigned char* PiCamera::Impl::planeData(void* address, const LFrameBuffer::Plane& plane) {
    return static_cast<const unsigned char*>(address) + plane.offset;
}

void PiCamera::Impl::unmapPlane(void* address, const LFrameBuffer::Plane& plane) {
    if (address != MAP_FAILED) {
        munmap(address, plane.offset + plane.length);
    }
}

RawFrame PiCamera::Impl::convert(LFrameBuffer* buffer) {
    const LFrameMetadata& metadata = buffer->metadata();
    // Return type differs by libcamera version (vector vs Span); let the
    // compiler deduce it and instantiate the templated helpers to match.
    auto&& planes = buffer->planes();
    if (metadata.status != LFrameMetadata::FrameSuccess || planes.empty()) {
        return RawFrame();
    }
    if (!loggedPlanes) {
        loggedPlanes = true;
        std::cout << "PiCamera: first frame planes=" << planes.size();
        for (const auto& plane : planes) {
            std::cout << " len=" << plane.length << " offset=" << plane.offset;
        }
        std::cout << " colorSpace="
                  << (streamColorSpace ? streamColorSpace->toString() : std::string("none")) << std::endl;
    }
    RawFrame frame;
    if (pixelFormat == libcamera::formats::MJPEG) {
        frame.data = convertJpeg(planes, metadata);
        frame.format = PixelFormat::BGR;
    } else if (pixelFormat == libcamera::formats::NV12) {
        frame.data = convertNv12(planes);
        frame.format = PixelFormat::NV12;
    } else {
        frame.data = convertI420(planes);
        frame.format = PixelFormat::I420;
    }
    if (frame.data.empty()) {
        return RawFrame();
    }
    // Item 7: correct orientation once, post-conversion, so motion
    // detection and the writer both see upright pixels.
    if (!applyOrientation(frame, orientation) && !orientationWarned) {
        orientationWarned = true;
        std::cerr << "PiCamera: cannot apply orientation to " << width << "x" << height
                  << " frame; emitting unrotated" << std::endl;
    }
    frame.colorSpace = frameColorSpace;
    frame.timestamp = std::chrono::system_clock::now();
    return frame;
}

template <typename Planes>
cv::Mat PiCamera::Impl::convertNv12(const Planes& planes) {
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
        const unsigned char* y = planeData(yMap, planes[0]);
        const unsigned char* uv = planeData(uvMap, planes[1]);
        for (unsigned int row = 0; row < height; row++) {
            std::memcpy(yuv.row(row).ptr(), y + (std::size_t)row * yStride, width);
        }
        for (unsigned int row = 0; row < height / 2; row++) {
            std::memcpy(yuv.row(height + row).ptr(), uv + (std::size_t)row * uvStride, width);
        }
        dumpFrame(yuv, "nv12");
        result = std::move(yuv);
    }
    unmapPlane(yMap, planes[0]);
    unmapPlane(uvMap, planes[1]);
    return result;
}

template <typename Planes>
cv::Mat PiCamera::Impl::convertI420(const Planes& planes) {
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
        const unsigned char* y = planeData(yMap, planes[0]);
        const unsigned char* u = planeData(uMap, planes[1]);
        const unsigned char* v = planeData(vMap, planes[2]);
        for (unsigned int row = 0; row < height; row++) {
            std::memcpy(yuv.row(row).ptr(), y + (std::size_t)row * yStride, width);
        }
        // OpenCV reads a 3H/2 x W I420 Mat as FLAT PLANAR: Y (W*H bytes),
        // then U (W/2 * H/2), then V - verified against COLOR_BGR2YUV_I420
        // output (its chroma Mat rows are 2 packed chroma lines each).
        unsigned int chromaWidth = width / 2;
        unsigned char* chroma = yuv.data + (std::size_t)height * width;
        unsigned char* uDst = chroma;
        unsigned char* vDst = chroma + (std::size_t)chromaWidth * (height / 2);
        for (unsigned int row = 0; row < height / 2; row++) {
            std::memcpy(uDst + (std::size_t)row * chromaWidth, u + (std::size_t)row * cStride, chromaWidth);
            std::memcpy(vDst + (std::size_t)row * chromaWidth, v + (std::size_t)row * cStride, chromaWidth);
        }
        dumpFrame(yuv, "i420");
        result = std::move(yuv);
    }
    unmapPlane(yMap, planes[0]);
    unmapPlane(uMap, planes[1]);
    unmapPlane(vMap, planes[2]);
    return result;
}

template <typename Planes>
cv::Mat PiCamera::Impl::convertJpeg(const Planes& planes,
                                    const LFrameMetadata& metadata) {
    void* map = mapPlane(planes[0]);
    cv::Mat result;
    if (map != MAP_FAILED) {
        auto span = metadata.planes();
        std::size_t bytes = span.empty() ? planes[0].length : span[0].bytesused;
        bytes = std::min(bytes, static_cast<std::size_t>(planes[0].length));
        const unsigned char* begin = planeData(map, planes[0]);
        std::vector<unsigned char> encoded(begin, begin + bytes);
        result = cv::imdecode(encoded, cv::IMREAD_COLOR);
    }
    unmapPlane(map, planes[0]);
    return result;
}

