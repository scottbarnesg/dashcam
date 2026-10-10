#include "orientation.hpp"

#include <gtest/gtest.h>

TEST(orientation, parsesConfigValues) {
    CameraOrientation o = CameraOrientation::Rotate180;
    EXPECT_TRUE(parseCameraOrientation("auto", o));
    EXPECT_EQ(o, CameraOrientation::Auto);
    EXPECT_TRUE(parseCameraOrientation("0", o));
    EXPECT_EQ(o, CameraOrientation::Rotate0);
    EXPECT_TRUE(parseCameraOrientation("90", o));
    EXPECT_EQ(o, CameraOrientation::Rotate90);
    EXPECT_TRUE(parseCameraOrientation("270", o));
    EXPECT_EQ(o, CameraOrientation::Rotate270);
    EXPECT_TRUE(parseCameraOrientation("mirror-180", o));
    EXPECT_EQ(o, CameraOrientation::Mirror180);
    EXPECT_FALSE(parseCameraOrientation("45", o));   // keeps previous value
    EXPECT_EQ(o, CameraOrientation::Mirror180);
    EXPECT_FALSE(parseCameraOrientation("", o));
    EXPECT_FALSE(parseCameraOrientation("mirror-45", o));
    EXPECT_FALSE(parseCameraOrientation("AUTO", o));
}

TEST(orientation, mapsToFlipAndRotateCode) {
    OrientationTransform identity = transformFor(CameraOrientation::Auto);
    EXPECT_TRUE(identity.identity());

    OrientationTransform r0 = transformFor(CameraOrientation::Rotate0);
    EXPECT_TRUE(r0.identity());

    OrientationTransform r90 = transformFor(CameraOrientation::Rotate90);
    EXPECT_FALSE(r90.mirror);
    EXPECT_EQ(r90.rotateCode, cv::ROTATE_90_CLOCKWISE);

    OrientationTransform r180 = transformFor(CameraOrientation::Rotate180);
    EXPECT_FALSE(r180.mirror);
    EXPECT_EQ(r180.rotateCode, cv::ROTATE_180);

    OrientationTransform r270 = transformFor(CameraOrientation::Rotate270);
    EXPECT_FALSE(r270.mirror);
    EXPECT_EQ(r270.rotateCode, cv::ROTATE_90_COUNTERCLOCKWISE);

    OrientationTransform m0 = transformFor(CameraOrientation::Mirror0);
    EXPECT_TRUE(m0.mirror);
    EXPECT_EQ(m0.rotateCode, -1);

    OrientationTransform m90 = transformFor(CameraOrientation::Mirror90);
    EXPECT_TRUE(m90.mirror);
    EXPECT_EQ(m90.rotateCode, cv::ROTATE_90_CLOCKWISE);

    OrientationTransform m270 = transformFor(CameraOrientation::Mirror270);
    EXPECT_TRUE(m270.mirror);
    EXPECT_EQ(m270.rotateCode, cv::ROTATE_90_COUNTERCLOCKWISE);
}

TEST(orientation, convertsCounterClockwiseRotationProperty) {
    // libcamera reports the correction as counter-clockwise degrees; our
    // enum is clockwise.
    EXPECT_EQ(orientationFromRotationProperty(0), CameraOrientation::Rotate0);
    EXPECT_EQ(orientationFromRotationProperty(90), CameraOrientation::Rotate270);
    EXPECT_EQ(orientationFromRotationProperty(180), CameraOrientation::Rotate180);
    EXPECT_EQ(orientationFromRotationProperty(270), CameraOrientation::Rotate90);
    // Out-of-range values normalize.
    EXPECT_EQ(orientationFromRotationProperty(-90), CameraOrientation::Rotate90);
    EXPECT_EQ(orientationFromRotationProperty(450), CameraOrientation::Rotate270);
    // Non-right angles snap to the nearest multiple of 90.
    EXPECT_EQ(orientationFromRotationProperty(100), CameraOrientation::Rotate270);
}

namespace {

// A packed 4x2 NV12 frame with distinct, asymmetric luma and chroma so a
// transform is verifiable pixel-by-pixel.
RawFrame makeNv12() {
    RawFrame frame;
    frame.format = PixelFormat::NV12;
    frame.data = cv::Mat(3, 4, CV_8UC1, cv::Scalar(0));
    for (int col = 0; col < 4; col++) {
        frame.data.at<uchar>(0, col) = static_cast<uchar>(10 + col);
        frame.data.at<uchar>(1, col) = static_cast<uchar>(20 + col);
    }
    frame.data.at<uchar>(2, 0) = 100; // U of chroma pixel (0,0)
    frame.data.at<uchar>(2, 1) = 200; // V of chroma pixel (0,0)
    frame.data.at<uchar>(2, 2) = 111; // U of chroma pixel (1,0)
    frame.data.at<uchar>(2, 3) = 222; // V of chroma pixel (1,0)
    return frame;
}

cv::Mat luma(const cv::Mat& packed) {
    int height = packed.rows * 2 / 3;
    return packed.rowRange(0, height).clone();
}

cv::Mat chroma(const cv::Mat& packed) {
    int height = packed.rows * 2 / 3;
    return packed.rowRange(height, height + height / 2).reshape(2, height / 2).clone();
}

} // namespace

TEST(orientation, rotate90AppliesToNv12LumaAndChroma) {
    RawFrame frame = makeNv12();
    cv::Mat lumaBefore = luma(frame.data);
    cv::Mat chromaBefore = chroma(frame.data);

    ASSERT_TRUE(applyOrientation(frame, CameraOrientation::Rotate90));

    EXPECT_EQ(frame.size(), cv::Size(2, 4));
    cv::Mat expectedLuma;
    cv::rotate(lumaBefore, expectedLuma, cv::ROTATE_90_CLOCKWISE);
    EXPECT_TRUE(0 == cv::sum(luma(frame.data) != expectedLuma)[0]);
    cv::Mat expectedChroma;
    cv::rotate(chromaBefore, expectedChroma, cv::ROTATE_90_CLOCKWISE);
    EXPECT_EQ(chroma(frame.data).total(), expectedChroma.total());
    EXPECT_TRUE(cv::norm(chroma(frame.data), expectedChroma, cv::NORM_INF) == 0);
}

TEST(orientation, mirrorFlipsNv12) {
    RawFrame frame = makeNv12();
    cv::Mat lumaBefore = luma(frame.data);
    cv::Mat chromaBefore = chroma(frame.data);

    ASSERT_TRUE(applyOrientation(frame, CameraOrientation::Mirror0));

    EXPECT_EQ(frame.size(), cv::Size(4, 2)); // Same dimensions, swapped columns.
    cv::Mat expectedLuma;
    cv::flip(lumaBefore, expectedLuma, 1);
    EXPECT_TRUE(cv::norm(luma(frame.data), expectedLuma, cv::NORM_INF) == 0);
    cv::Mat expectedChroma;
    cv::flip(chromaBefore, expectedChroma, 1);
    EXPECT_TRUE(cv::norm(chroma(frame.data).reshape(1), expectedChroma.reshape(1), cv::NORM_INF) == 0);
}

TEST(orientation, autoAndIdentityLeaveNv12Untouched) {
    RawFrame frame = makeNv12();
    cv::Mat before = frame.data.clone();
    EXPECT_TRUE(applyOrientation(frame, CameraOrientation::Auto));
    EXPECT_TRUE(applyOrientation(frame, CameraOrientation::Rotate0));
    EXPECT_TRUE(cv::norm(frame.data, before, cv::NORM_INF) == 0);
}

TEST(orientation, rotate180AppliesToI420) {
    // Packed I420 4x2: Y (4x2), then U (2x1), V (2x1).
    RawFrame frame;
    frame.format = PixelFormat::I420;
    frame.data = cv::Mat(3, 4, CV_8UC1, cv::Scalar(0));
    frame.data.at<uchar>(0, 0) = 7;
    frame.data.at<uchar>(1, 3) = 9;
    frame.data.at<uchar>(2, 0) = 11; // U(0,0)
    frame.data.at<uchar>(2, 1) = 22; // U(1,0)
    cv::Mat before = frame.data.clone();

    ASSERT_TRUE(applyOrientation(frame, CameraOrientation::Rotate180));

    EXPECT_EQ(frame.size(), cv::Size(4, 2));
    // 180 flips Y order and chroma order: Y(0,0) ends up at Y(1,3), U(0,0) at U(1,0).
    EXPECT_EQ(frame.data.at<uchar>(1, 3), 7);
    EXPECT_EQ(frame.data.at<uchar>(0, 0), 9);
    EXPECT_EQ(frame.data.at<uchar>(2, 1), 11);
    EXPECT_EQ(frame.data.at<uchar>(2, 0), 22);
    EXPECT_FALSE(cv::countNonZero(frame.data != before) == 0);
}

TEST(orientation, rotate90AppliesToBgr) {
    RawFrame frame;
    frame.format = PixelFormat::BGR;
    frame.data = cv::Mat(2, 4, CV_8UC3, cv::Scalar(0, 0, 0));
    frame.data.at<cv::Vec3b>(0, 0) = cv::Vec3b(1, 2, 3);
    cv::Mat expected;
    cv::rotate(frame.data, expected, cv::ROTATE_90_CLOCKWISE);

    ASSERT_TRUE(applyOrientation(frame, CameraOrientation::Rotate90));
    EXPECT_EQ(frame.size(), cv::Size(2, 4));
    EXPECT_TRUE(cv::norm(frame.data, expected, cv::NORM_INF) == 0);
}
