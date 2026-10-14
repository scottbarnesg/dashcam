// Fragmented-MP4 muxer over the H.264 output of the hardware encoder, using
// libavformat's mov muxer. Fragments close on keyframes (matching the
// encoder's IDR period), which keeps everything up to the last flushed
// fragment playable even if the trailer is never written (power loss).

#include "mp4muxer.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

#ifdef DASHCAM_HAVE_LIBAV
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}
#endif

namespace {

struct StartCode {
    std::size_t offset; // First 0x00 of the sequence.
    std::size_t length; // 3 or 4.
};

// Finds the start code at or after 'from', scanning byte-wise. Annex-B
// streams only carry 3/4-byte start codes before each NAL unit.
std::optional<StartCode> findStartCode(const uint8_t* data, std::size_t size, std::size_t from) {
    for (std::size_t i = from; i + 2 < size; i++) {
        if (data[i] == 0 && data[i + 1] == 0) {
            if (data[i + 2] == 1) {
                return StartCode{i, 3};
            }
            if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
                return StartCode{i, 4};
            }
        }
    }
    return std::nullopt;
}

void be16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}

void be32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}

constexpr int kIoBufferSize = 64 * 1024;

} // namespace

namespace mp4 {

bool findParameterSets(const uint8_t* data, std::size_t size,
                       const uint8_t** sps, std::size_t* spsSize,
                       const uint8_t** pps, std::size_t* ppsSize) {
    *sps = *pps = nullptr;
    *spsSize = *ppsSize = 0;
    auto sc = findStartCode(data, size, 0);
    if (!sc || sc->offset != 0) {
        return false; // Must start with a start code.
    }
    while (true) {
        std::size_t nalStart = sc->offset + sc->length;
        if (nalStart >= size) {
            break;
        }
        auto next = findStartCode(data, size, nalStart);
        std::size_t nalEnd = next ? next->offset : size;
        uint8_t type = data[nalStart] & 0x1F;
        std::size_t len = nalEnd - nalStart;
        if (type == 7 && !*sps) {
            *sps = data + nalStart;
            *spsSize = len;
        } else if (type == 8 && !*pps) {
            *pps = data + nalStart;
            *ppsSize = len;
        }
        if (*sps && *pps) {
            return true;
        }
        if (!next) {
            break;
        }
        sc = next;
    }
    return *sps && *pps;
}

std::vector<std::uint8_t> buildAvcC(const uint8_t* sps, std::size_t spsSize,
                                    const uint8_t* pps, std::size_t ppsSize) {
    std::vector<std::uint8_t> avc;
    avc.push_back(1);                       // configurationRevision
    avc.push_back(sps[1]);                  // AVCProfileIndication
    avc.push_back(sps[2]);                  // profile_compatibility
    avc.push_back(sps[3]);                  // AVCLevelIndication
    avc.push_back(0xFF);                    // 6 bits reserved | lengthSizeMinusOne = 3
    avc.push_back(0xE1);                    // 3 bits reserved | numOfSequenceParameterSets = 1
    be16(avc, static_cast<std::uint16_t>(spsSize));
    avc.insert(avc.end(), sps, sps + spsSize);
    avc.push_back(1);                       // numOfPictureParameterSets
    be16(avc, static_cast<std::uint16_t>(ppsSize));
    avc.insert(avc.end(), pps, pps + ppsSize);
    return avc;
}

bool annexBToLengthPrefixed(const uint8_t* data, std::size_t size,
                            std::vector<std::uint8_t>& out, bool& containsIdr) {
    containsIdr = false;
    auto sc = findStartCode(data, size, 0);
    if (!sc || sc->offset != 0) {
        return false;
    }
    bool any = false;
    while (true) {
        std::size_t nalStart = sc->offset + sc->length;
        if (nalStart >= size) {
            break;
        }
        auto next = findStartCode(data, size, nalStart);
        std::size_t nalEnd = next ? next->offset : size;
        if (nalEnd > nalStart) {
            // Trim trailing zero padding absorbed from the next start code.
            while (nalEnd > nalStart + 1 && data[nalEnd - 1] == 0) {
                nalEnd--;
            }
            if ((data[nalStart] & 0x1F) == 5) {
                containsIdr = true;
            }
            be32(out, static_cast<std::uint32_t>(nalEnd - nalStart));
            out.insert(out.end(), data + nalStart, data + nalEnd);
            any = true;
        }
        if (!next) {
            break;
        }
        sc = next;
    }
    return any;
}

} // namespace mp4

Mp4Muxer::Mp4Muxer(std::filesystem::path path, int width, int height, int fps)
    : filePath(std::move(path)), width(width), height(height), fps(fps > 0 ? fps : 30) {
}

Mp4Muxer::~Mp4Muxer() {
    close();
}

void Mp4Muxer::fail(const std::string& message) {
    if (!failed) {
        failed = true;
        errorMessage = message;
        std::cerr << "Mp4Muxer: " << message << std::endl;
    }
}

#ifdef DASHCAM_HAVE_LIBAV

int Mp4Muxer::ioWrite(void* opaque, uint8_t* buf, int size) {
    Mp4Muxer* self = static_cast<Mp4Muxer*>(opaque);
    ssize_t written = ::write(self->fileDescriptor, buf, static_cast<size_t>(size));
    if (written < 0) {
        return AVERROR(errno);
    }
    return static_cast<int>(written);
}

std::int64_t Mp4Muxer::ioSeek(void* opaque, std::int64_t offset, int whence) {
    Mp4Muxer* self = static_cast<Mp4Muxer*>(opaque);
    if (whence == AVSEEK_SIZE) {
        off_t size = lseek(self->fileDescriptor, 0, SEEK_END);
        return size < 0 ? AVERROR(errno) : static_cast<std::int64_t>(size);
    }
    off_t off = lseek(self->fileDescriptor, static_cast<off_t>(offset), whence);
    return off < 0 ? AVERROR(errno) : static_cast<std::int64_t>(off);
}

void Mp4Muxer::open() {
    if (opened || failed) {
        return;
    }
    // Own the file descriptor: the muxer's data must be fsync()ed at every
    // fragment boundary, and avio gives no handle to the underlying fd
    // (avio_get_fd was removed in modern libavformat anyway).
    int fd = ::open(filePath.string().c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        throw std::runtime_error("could not open " + filePath.string() + ": " + std::strerror(errno));
    }
    fileDescriptor = fd;
    unsigned char* buffer = static_cast<unsigned char*>(av_malloc(kIoBufferSize));
    AVIOContext* io = buffer ? avio_alloc_context(buffer, kIoBufferSize, 1, this, nullptr, ioWrite, ioSeek)
                             : nullptr;
    if (!io) {
        av_free(buffer);
        ::close(fd);
        fileDescriptor = -1;
        throw std::runtime_error(std::string("could not create ") + filePath.string());
    }
    ioContext = io;

    AVFormatContext* fmt = nullptr;
    int ret = avformat_alloc_output_context2(&fmt, nullptr, "mp4", filePath.string().c_str());
    if (ret < 0 || !fmt) {
        releaseIo();
        throw std::runtime_error("Mp4Muxer: could not create mp4 output context");
    }
    AVStream* stream = avformat_new_stream(fmt, nullptr);
    if (!stream) {
        avformat_free_context(fmt);
        releaseIo();
        formatContext = nullptr;
        throw std::runtime_error("Mp4Muxer: could not create stream");
    }
    AVCodecParameters* par = stream->codecpar;
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = AV_CODEC_ID_H264;
    par->format = AV_PIX_FMT_YUV420P;
    par->width = static_cast<int>(width);
    par->height = static_cast<int>(height);
    stream->time_base = AVRational{1, fps};
    fmt->pb = io;
    formatContext = fmt;
    opened = true;
}

bool Mp4Muxer::writeHeader(const uint8_t* annexB, std::size_t size) {
    AVFormatContext* fmt = static_cast<AVFormatContext*>(formatContext);
    const uint8_t* sps = nullptr;
    const uint8_t* pps = nullptr;
    std::size_t spsSize = 0;
    std::size_t ppsSize = 0;
    if (!mp4::findParameterSets(annexB, size, &sps, &spsSize, &pps, &ppsSize)) {
        fail("first access unit carries no SPS/PPS");
        return false;
    }
    std::vector<std::uint8_t> avc = mp4::buildAvcC(sps, spsSize, pps, ppsSize);
    AVCodecParameters* par = fmt->streams[0]->codecpar;
    par->extradata = static_cast<uint8_t*>(av_malloc(avc.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!par->extradata) {
        fail("av_malloc failed for extradata");
        return false;
    }
    std::memcpy(par->extradata, avc.data(), avc.size());
    par->extradata_size = static_cast<int>(avc.size());

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    int ret = avformat_write_header(fmt, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        fail(std::string("avformat_write_header failed: ") + err);
        return false;
    }
    headerWritten = true;
    return true;
}

void Mp4Muxer::write(const uint8_t* annexB, std::size_t size, bool keyframe, std::int64_t captureUs) {
    if (!opened || failed) {
        return;
    }
    if (!headerWritten && !writeHeader(annexB, size)) {
        return;
    }
    AVFormatContext* fmt = static_cast<AVFormatContext*>(formatContext);
    AVStream* stream = fmt->streams[0];

    scratch.clear();
    bool idr = false;
    if (!mp4::annexBToLengthPrefixed(annexB, size, scratch, idr)) {
        return; // Empty access unit; skip.
    }
    AVPacket* packet = av_packet_alloc();
    if (!packet || av_new_packet(packet, static_cast<int>(scratch.size())) < 0) {
        av_packet_free(&packet);
        fail("packet allocation failed");
        return;
    }
    std::memcpy(packet->data, scratch.data(), scratch.size());
    packet->stream_index = stream->index;
    std::int64_t pts;
    if (captureUs >= 0) {
        if (!haveFirstCapture) {
            haveFirstCapture = true;
            firstCaptureUs = captureUs;
        }
        pts = av_rescale_q(captureUs - firstCaptureUs, AVRational{1, 1000000}, stream->time_base);
    } else {
        pts = av_rescale_q(nextPts, AVRational{1, fps}, stream->time_base);
    }
    if (pts <= lastPts) {
        pts = lastPts + 1; // Strict monotonicity guard (duplicate/zero stamps).
    }
    // Each sample must span the real time until the NEXT sample, not a fixed
    // one-tick duration. PTS follow capture clocks, so at below-nominal
    // capture rates (and whenever the capture queue drops a frame) samples
    // are spaced further apart than one tick. With a fixed duration of 1 the
    // timeline has unfilled gaps, which strict/hardware players render as
    // brief grey flashes (ffmpeg fills the gap with the last frame, hiding
    // it). Duration = the PTS delta to the previous sample closes the hole.
    std::int64_t duration = (lastPts >= 0) ? (pts - lastPts) : 1;
    if (duration < 1) {
        duration = 1;
    }
    packet->pts = packet->dts = pts;
    packet->duration = static_cast<int>(duration);
    nextPts++;
    lastPts = pts;
    if (keyframe || idr) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }
    int ret = av_interleaved_write_frame(fmt, packet);
    av_packet_free(&packet);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        fail(std::string("av_interleaved_write_frame failed: ") + err);
        return;
    }
    // Fragment boundary: the mov muxer finalizes the previous fragment when
    // it accepts this keyframe packet. Push it all the way to stable media so
    // a power cut cannot lose already-encoded footage that older fragments
    // covered. avio_flush() alone only reaches the kernel page cache, which a
    // hard power cut takes with it: dirty pages linger there for up to the
    // kernel's writeback expiry (~30 s default), which is exactly how much a
    // hard cut can steal no matter how short the GOP is. The fsync pins each
    // closed fragment to the card as soon as it is complete.
    if (keyframe || idr) {
        avio_flush(fmt->pb);
        if (fsync(fileDescriptor) != 0) {
            std::cerr << "Mp4Muxer: fsync failed: " << std::strerror(errno) << std::endl;
        }
    }
}

void Mp4Muxer::releaseIo() {
    if (ioContext) {
        AVIOContext* io = static_cast<AVIOContext*>(ioContext);
        av_free(io->buffer);
        avio_context_free(&io);
        ioContext = nullptr;
    }
    if (fileDescriptor >= 0) {
        ::close(fileDescriptor);
        fileDescriptor = -1;
    }
}

void Mp4Muxer::close() {
    AVFormatContext* fmt = static_cast<AVFormatContext*>(formatContext);
    if (fmt) {
        if (headerWritten) {
            int ret = av_write_trailer(fmt);
            if (ret < 0 && !failed) {
                char err[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, err, sizeof(err));
                fail(std::string("av_write_trailer failed: ") + err);
            }
            // A cleanly closed segment must survive a later power cut too
            // (it gets registered in the manifest immediately after).
            avio_flush(fmt->pb);
            if (fileDescriptor >= 0 && fsync(fileDescriptor) != 0 && !failed) {
                fail(std::string("fsync after trailer failed: ") + std::strerror(errno));
            }
        }
        avformat_free_context(fmt);
        formatContext = nullptr;
    }
    releaseIo();
    opened = false;
}

#else // !DASHCAM_HAVE_LIBAV

void Mp4Muxer::open() {
    throw std::runtime_error("Mp4Muxer: built without libavformat (hardware encoding unavailable)");
}

bool Mp4Muxer::writeHeader(const uint8_t*, std::size_t) {
    return false;
}

void Mp4Muxer::write(const uint8_t*, std::size_t, bool, std::int64_t) {
}

void Mp4Muxer::close() {
    opened = false;
}

void Mp4Muxer::releaseIo() {
}

#endif
