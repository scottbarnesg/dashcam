// Pins down the packed-I420 Mat layout that PiCamera::Impl::convertI420
// assembles frame buffers into (Y plane, then per uv row: U row then V row).
// A regression here crashes or colorizes every YUV420 capture.
#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

TEST(PackedI420, RowsInterleaveUvAndConvertCleanly) {
    const int width = 64, height = 32;
    cv::Mat yuv(height * 3 / 2, width, CV_8UC1);

    // Known pattern: Y = 16+row, U = 128+col, V = 64+col.
    for (int row = 0; row < height; row++) {
        std::memset(yuv.row(row).ptr(), 16 + row, width);
    }
    for (int row = 0; row < height / 2; row++) {
        unsigned char* dst = yuv.row(height + row).ptr();
        for (int col = 0; col < width / 2; col++) {
            dst[col] = 128 + col;
            dst[width / 2 + col] = 64 + col;
        }
    }

    cv::Mat bgr;
    EXPECT_NO_THROW(cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420));
    ASSERT_EQ(bgr.size(), cv::Size(width, height));
    ASSERT_EQ(bgr.channels(), 3);

    // Rows differ in Y only, so BGR must differ row to row but be constant
    // along a row within each half-resolution chroma cell (cols 4/5 share one).
    EXPECT_NE(bgr.at<cv::Vec3b>(4, 10), bgr.at<cv::Vec3b>(10, 10));
    EXPECT_EQ(bgr.at<cv::Vec3b>(10, 4), bgr.at<cv::Vec3b>(10, 5));
}
