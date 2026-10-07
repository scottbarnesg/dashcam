#include "camera.hpp"

#include <gtest/gtest.h>

TEST(CameraFactory, UnknownBackendThrows) {
    EXPECT_THROW(createCamera("nonexistent"), std::runtime_error);
}

TEST(CameraFactory, PiBackendOnlyWhenBuilt) {
#ifdef USE_PI_CAMERA
    EXPECT_EQ(createCamera("pi")->name(), "pi");
#else
    EXPECT_THROW(createCamera("pi"), std::runtime_error);
#endif
}
