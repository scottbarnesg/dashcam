#include <opencv2/imgproc.hpp>
#include <string>

#include "frame.hpp"

#ifndef ORIENTATION_HPP
#define ORIENTATION_HPP

// Camera image orientation (BACKLOG item 7). Mirrors the config values of
// "camera_orientation": Rotate* means "rotate the frame clockwise by N
// degrees"; Mirror* means "flip horizontally, then rotate clockwise by N
// degrees" (the mirror/transpose variants of the EXIF orientation set).
// Auto defers to what the sensor reports at configure time and falls back
// to Rotate0 when the sensor reports nothing.
enum class CameraOrientation {
    Auto,
    Rotate0,
    Rotate90,
    Rotate180,
    Rotate270,
    Mirror0,
    Mirror90,
    Mirror180,
    Mirror270,
};

// Parses a camera_orientation config value: "auto", "0", "90", "180",
// "270", "mirror-0", "mirror-90", "mirror-180", "mirror-270".
// Returns false (leaving out untouched) for unknown values.
bool parseCameraOrientation(const std::string& value, CameraOrientation& out);

// Pure orientation -> pixel-op mapping, unit-tested in isolation:
// horizontal flip (applied first) plus a rotation given as a
// cv::RotateCode, or -1 when no rotation is needed.
struct OrientationTransform {
    bool mirror = false;
    int rotateCode = -1;

    bool identity() const { return !mirror && rotateCode < 0; }
};

OrientationTransform transformFor(CameraOrientation orientation);

// Converts a libcamera "Rotation" camera property (counter-clockwise
// degrees, documented range [0, 360[; arbitrary values are normalized)
// into an orientation: a correction of R degrees CCW is a clockwise
// rotation by 360-R.
CameraOrientation orientationFromRotationProperty(int ccwDegrees);

// Applies the orientation to a captured frame (packed formats per
// frame.hpp) so motion detection and the writer consume correctly
// oriented pixels. Auto is treated as Rotate0; callers resolve Auto
// against the sensor first. Returns false and leaves the frame
// untouched for formats this cannot transform (odd dimensions).
bool applyOrientation(RawFrame& frame, CameraOrientation orientation);

#endif
