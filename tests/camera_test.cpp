#include "camera.hpp"

#include <gtest/gtest.h>

TEST(CameraFactory, UnknownBackendThrows) {
    EXPECT_THROW(createCamera("nonexistent"), std::runtime_error);
}

TEST(CameraFactory, PiBackendRegistered) {
    EXPECT_EQ(createCamera("pi")->name(), "pi");
}
