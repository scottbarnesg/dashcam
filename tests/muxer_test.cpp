#include "mp4muxer.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {

// Build an Annex-B byte stream from raw NAL units (adds 4-byte start codes).
std::vector<uint8_t> annexB(const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& nals) {
    std::vector<uint8_t> out;
    for (const auto& [type, payload] : nals) {
        out.insert(out.end(), {0, 0, 0, 1, type});
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

std::vector<uint8_t> fakeSps() {
    // Header bytes carry the profile/compat/level the avcC record copies.
    return {0x64, 0x00, 0x28, 0xAB}; // type byte (7) is added via annexB pairs
}

} // namespace

TEST(Mp4AnnexB, FindsParameterSets) {
    auto spsPayload = fakeSps();
    std::vector<uint8_t> ppsPayload{0xCE, 0x01};
    // IDR sandwiched first: the scan must find 7/8 anywhere in the AU.
    auto stream = annexB({{5, {1, 2, 3}}, {7, spsPayload}, {8, ppsPayload}});
    const uint8_t *sps, *pps;
    std::size_t spsSize, ppsSize;
    ASSERT_TRUE(mp4::findParameterSets(stream.data(), stream.size(), &sps, &spsSize, &pps, &ppsSize));
    EXPECT_EQ(sps[0] & 0x1F, 7u);
    EXPECT_EQ(spsSize, spsPayload.size() + 1);
    EXPECT_EQ(std::memcmp(sps + 1, spsPayload.data(), spsPayload.size()), 0);
    EXPECT_EQ(pps[0] & 0x1F, 8u);
}

TEST(Mp4AnnexB, MissingParameterSetsFails) {
    auto stream = annexB({{5, {1, 2, 3}}});
    const uint8_t *sps, *pps;
    std::size_t spsSize, ppsSize;
    EXPECT_FALSE(mp4::findParameterSets(stream.data(), stream.size(), &sps, &spsSize, &pps, &ppsSize));
}

TEST(Mp4AnnexB, RejectsNonAnnexB) {
    std::vector<uint8_t> junk{1, 2, 3, 4, 5};
    const uint8_t *sps, *pps;
    std::size_t spsSize, ppsSize;
    EXPECT_FALSE(mp4::findParameterSets(junk.data(), junk.size(), &sps, &spsSize, &pps, &ppsSize));
}

TEST(Mp4AvcC, MatchesReferenceLayout) {
    auto spsPayload = fakeSps();
    std::vector<uint8_t> spsNal;
    spsNal.push_back(7);
    spsNal.insert(spsNal.end(), spsPayload.begin(), spsPayload.end());
    std::vector<uint8_t> ppsNal{8, 0xCE};
    auto avc = mp4::buildAvcC(spsNal.data(), spsNal.size(), ppsNal.data(), ppsNal.size());

    ASSERT_EQ(avc.size(), 8u + spsNal.size() + 3u + ppsNal.size());
    EXPECT_EQ(avc[0], 1u);
    EXPECT_EQ(avc[1], 0x64u); // profile from SPS[1]
    EXPECT_EQ(avc[2], 0x00u); // compat  from SPS[2]
    EXPECT_EQ(avc[3], 0x28u); // level   from SPS[3]
    EXPECT_EQ(avc[4], 0xFFu); // 4-byte NAL lengths
    EXPECT_EQ(avc[5], 0xE1u); // one SPS
    EXPECT_EQ((avc[6] << 8) | avc[7], spsNal.size());
    EXPECT_EQ(std::memcmp(&avc[8], spsNal.data(), spsNal.size()), 0);
    std::size_t p = 8 + spsNal.size();
    EXPECT_EQ(avc[p], 1u); // one PPS
    EXPECT_EQ((avc[p + 1] << 8) | avc[p + 2], ppsNal.size());
    EXPECT_EQ(std::memcmp(&avc[p + 3], ppsNal.data(), ppsNal.size()), 0);
}

TEST(Mp4LengthPrefix, ConvertsAndDetectsIdr) {
    auto stream = annexB({{7, {0x64, 0, 0x28}}, {8, {0xCE}}, {5, {0x41, 0xA0, 1, 2}}});
    std::vector<uint8_t> out;
    bool idr = false;
    ASSERT_TRUE(mp4::annexBToLengthPrefixed(stream.data(), stream.size(), out, idr));
    EXPECT_TRUE(idr);

    // Walk the length-prefixed units back.
    std::size_t pos = 0;
    int units = 0;
    while (pos < out.size()) {
        ASSERT_LE(pos + 4, out.size());
        uint32_t len = (uint32_t(out[pos]) << 24) | (uint32_t(out[pos + 1]) << 16) |
                       (uint32_t(out[pos + 2]) << 8) | out[pos + 3];
        ASSERT_LE(pos + 4 + len, out.size());
        EXPECT_NE(out[pos + 4] & 0x1F, 0u);
        pos += 4 + len;
        units++;
    }
    EXPECT_EQ(units, 3);
}

TEST(Mp4LengthPrefix, EmptyAuYieldsNothing) {
    std::vector<uint8_t> empty;
    std::vector<uint8_t> out;
    bool idr = true;
    EXPECT_FALSE(mp4::annexBToLengthPrefixed(empty.data(), empty.size(), out, idr));
    EXPECT_TRUE(out.empty());
}
