#include "naming.hpp"

#include <cstdio>
#include <ctime>
#include <regex>

namespace naming {

std::string timestamp(std::chrono::system_clock::time_point startTime) {
    std::time_t t = std::chrono::system_clock::to_time_t(startTime);
    std::tm tmBuf;
    localtime_r(&t, &tmBuf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmBuf);
    return std::string(buf);
}

std::string fileName(std::chrono::system_clock::time_point startTime,
                     const std::string& context,
                     unsigned long sequence) {
    char seq[16];
    std::snprintf(seq, sizeof(seq), "%04lu", sequence);
    return "dashcam_" + context + "_" + timestamp(startTime) + "_" + seq + ".mp4";
}

bool matches(const std::string& name) {
    static const std::regex pattern(R"(dashcam_[a-z]+_\d{8}-\d{6}_\d{4}\.mp4)");
    return std::regex_match(name, pattern);
}

} // namespace naming
