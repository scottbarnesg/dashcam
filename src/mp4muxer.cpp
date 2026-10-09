// Fragmented-MP4 muxer over the H.264 output of the hardware encoder, using
// libavformat's mov muxer. Fragments close on keyframes (matching the
// encoder's IDR period), which keeps everything up to the last flushed
// fragment playable even if the trailer is never written (power loss).

#include "mp4muxer.hpp"

#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>

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

void Mp4Muxer::open() {
    if (opened || failed) {
        return;
    }
    AVIOContext* io = nullptr;
    int ret = avio_open2(&io, filePath.string().c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        throw std::runtime_error(std::string("could not open ") + filePath.string() + ": " + err);
    }
    ioContext = io;

    AVFormatContext* fmt = nullptr;
    ret = avformat_alloc_output_context2(&fmt, nullptr, "mp4", filePath.string().c_str());
    if (ret < 0 || !fmt) {
        avio_closep(&io);
        ioContext = nullptr;
        throw std::runtime_error("Mp4Muxer: could not create mp4 output context");
    }
    AVStream* stream = avformat_new_stream(fmt, nullptr);
    if (!stream) {
        avformat_free_context(fmt);
        avio_closep(&io);
        ioContext = nullptr;
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

void Mp4Muxer::write(const uint8_t* annexB, std::size_t size, bool keyframe) {
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
    AVRational frameRate{1, fps};
    packet->pts = packet->dts = av_rescale_q(nextPts, frameRate, stream->time_base);
    packet->duration = av_rescale_q(nextPts + 1, frameRate, stream->time_base) - packet->pts;
    if (packet->duration < 1) {
        packet->duration = 1;
    }
    nextPts++;
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
    // it accepts this keyframe packet. Push it out of the userspace buffer so
    // a power cut cannot lose already-encoded footage that older fragments
    // covered.
    if (keyframe) {
        avio_flush(fmt->pb);
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
        }
        avformat_free_context(fmt);
        formatContext = nullptr;
    }
    if (ioContext) {
        AVIOContext* io = static_cast<AVIOContext*>(ioContext);
        avio_closep(&io);
        ioContext = nullptr;
    }
    opened = false;
}

#else // !DASHCAM_HAVE_LIBAV

void Mp4Muxer::open() {
    throw std::runtime_error("Mp4Muxer: built without libavformat (hardware encoding unavailable)");
}

bool Mp4Muxer::writeHeader(const uint8_t*, std::size_t) {
    return false;
}

void Mp4Muxer::write(const uint8_t*, std::size_t, bool) {
}

void Mp4Muxer::close() {
    opened = false;
}

#endif
