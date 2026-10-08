// Pins down the packed-I420 Mat layout that PiCamera::Impl::convertI420
// assembles frame buffers into: OpenCV reads the chroma region as FLAT
// PLANAR (U plane then V plane), NOT U/V rows interleaved per Mat row.
// Verified empirically against OpenCV's own COLOR_BGR2YUV_I420 output.
#include <cstring>
#include <vector>

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

namespace {
// Assemble an I420 "Mat buffer" the way convertI420 does from planes.
cv::Mat assembleI420(int W, int H, const unsigned char* y, unsigned int yStride,
                     const unsigned char* u, unsigned int cStride, const unsigned char* v) {
    cv::Mat yuv(H * 3 / 2, W, CV_8UC1);
    for (int row = 0; row < H; row++) {
        std::memcpy(yuv.row(row).ptr(), y + (std::size_t)row * yStride, W);
    }
    unsigned int chromaWidth = W / 2;
    unsigned char* chroma = yuv.data + (std::size_t)H * W;
    unsigned char* uDst = chroma;
    unsigned char* vDst = chroma + (std::size_t)chromaWidth * (H / 2);
    for (int row = 0; row < H / 2; row++) {
        std::memcpy(uDst + (std::size_t)row * chromaWidth, u + (std::size_t)row * cStride, chromaWidth);
        std::memcpy(vDst + (std::size_t)row * chromaWidth, v + (std::size_t)row * cStride, chromaWidth);
    }
    return yuv;
}
}

TEST(PackedI420, MatchesOpenCvOwnLayout) {
    const int W = 64, H = 32;
    cv::Mat bgr(H, W, CV_8UC3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bgr.at<cv::Vec3b>(y, x) = cv::Vec3b((x * 7) % 256, (y * 13) % 256, (x * y) % 256);
        }
    }
    cv::Mat reference;
    cv::cvtColor(bgr, reference, cv::COLOR_BGR2YUV_I420);

    // Feed OpenCV's own output back through the plane-extraction path:
    // Y plane + flat U plane + flat V plane must round-trip byte-identical.
    const unsigned char* refY = reference.data;
    const unsigned char* refU = refY + (std::size_t)W * H;
    const unsigned char* refV = refU + (std::size_t)(W / 2) * (H / 2);
    cv::Mat assembled = assembleI420(W, H, refY, W, refU, W / 2, refV);

    ASSERT_EQ(assembled.total(), reference.total());
    EXPECT_EQ(0, std::memcmp(assembled.data, reference.data, assembled.total()));
}

TEST(PackedI420, DecodesToExpectedColors) {
    const int W = 64, H = 32;
    // Solid red frame: Y~81, U~90, V~240 (OpenCV YUV, BT.601 full-ish).
    std::vector<unsigned char> y(W * H, 81), u((W / 2) * (H / 2), 90), v((W / 2) * (H / 2), 240);
    cv::Mat assembled = assembleI420(W, H, y.data(), W, u.data(), W / 2, v.data());
    cv::Mat bgr;
    cv::cvtColor(assembled, bgr, cv::COLOR_YUV2BGR_I420);
    cv::Vec3b center = bgr.at<cv::Vec3b>(16, 32);
    EXPECT_GT(center[2], 200);   // R high
    EXPECT_LT(center[0], 60);    // B low
    EXPECT_LT(center[1], 60);    // G low
}
