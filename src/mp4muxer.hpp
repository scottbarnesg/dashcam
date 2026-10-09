#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef MP4MUXER_HPP
#define MP4MUXER_HPP

namespace mp4 {

// Pure Annex-B helpers (unit-testable, no libav types).

// Locates the first SPS (NAL type 7) and PPS (NAL type 8) in an Annex-B
// access unit. Returns false if either is missing.
bool findParameterSets(const uint8_t* data, std::size_t size,
                       const uint8_t** sps, std::size_t* spsSize,
                       const uint8_t** pps, std::size_t* ppsSize);

// Builds an AVCDecoderConfigurationRecord (avcC, 4-byte NAL lengths) from
// raw SPS/PPS NAL units (no start codes).
std::vector<std::uint8_t> buildAvcC(const uint8_t* sps, std::size_t spsSize,
                                    const uint8_t* pps, std::size_t ppsSize);

// Converts an Annex-B access unit (start-code NAL units) to MP4
// length-prefixed form, appended to 'out'. Reports whether an IDR
// (NAL type 5) was seen.
bool annexBToLengthPrefixed(const uint8_t* data, std::size_t size,
                            std::vector<std::uint8_t>& out, bool& containsIdr);

} // namespace mp4

// Muxes Annex-B H.264 access units into a fragmented MP4 file via
// libavformat (mov muxer, frag_keyframe+empty_moov+default_base_moof).
// Fragmentation doubles as power-loss safety: once a fragment is
// flushed, everything up to it is playable even without the trailer.
//
// Not thread-safe: write() runs on the encoder's callback thread; the
// caller must drain the encoder before calling close() from its own
// thread. All libav failures are recorded, never thrown.
class Mp4Muxer {
    public:
        Mp4Muxer(std::filesystem::path path, int width, int height, int fps);
        ~Mp4Muxer();

        Mp4Muxer(const Mp4Muxer&) = delete;
        Mp4Muxer& operator=(const Mp4Muxer&) = delete;

        // Opens the container and creates the H.264 stream; the file header
        // itself is written on the first write() (SPS/PPS become extradata).
        // Throws std::runtime_error when the file/stream cannot be created.
        void open();

        // Muxes one encoded access unit. PTS come from the capture timestamp
        // when >= 0 (variable frame rate: transient input stalls become PTS
        // gaps instead of sped-up playback); negative stamps fall back to a
        // constant 1/fps cadence.
        void write(const uint8_t* annexB, std::size_t size, bool keyframe, std::int64_t captureUs = -1);

        // Writes the trailer (flushes the last fragment) and releases
        // everything. Safe to call more than once / after failed open.
        void close();

        bool isOpen() const { return opened; }
        bool ok() const { return !failed; }
        std::string lastError() const { return errorMessage; }

    private:
        bool writeHeader(const uint8_t* annexB, std::size_t size);

        std::filesystem::path filePath;
        int width, height, fps;
        void* formatContext = nullptr; // AVFormatContext* (kept out of header)
        void* ioContext = nullptr;     // AVIOContext*
    bool opened = false;
    bool headerWritten = false;
    bool failed = false;
    std::string errorMessage;
    std::int64_t nextPts = 0;
    std::int64_t firstCaptureUs = 0;
    bool haveFirstCapture = false;
    std::int64_t lastPts = -1;
    std::vector<std::uint8_t> scratch;

        void fail(const std::string& message);
};

#endif
