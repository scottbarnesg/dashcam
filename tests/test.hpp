#ifndef TEST_HPP
#define TEST_HPP

#include <iostream>

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(cond) do { \
    testsRun++; \
    if (!(cond)) { \
        testsFailed++; \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #cond << std::endl; \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    testsRun++; \
    if (!((a) == (b))) { \
        testsFailed++; \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #a << " == " << #b \
                  << " (" << (a) << " vs " << (b) << ")" << std::endl; \
    } \
} while (0)

#define TEST_RESULT() do { \
    std::cout << testsRun - testsFailed << "/" << testsRun << " checks passed" << std::endl; \
    return testsFailed == 0 ? 0 : 1; \
} while (0)

#endif
