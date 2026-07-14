#pragma once

#include <cmath>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <sstream>
#include <iostream>
#include <cstddef>

#include <glm/glm.hpp>

#include "preprocessing/gpu_datatypes.hpp"

namespace newstar {


namespace {

// Simple: write up to 32 lines of up to 32 chars (newline -> 10), unused cells = 0.
// s: input string, buf: 32*8 uint32_t buffer, returns used lines
size_t fillTextBufferPacked(const std::string &s, uint32_t buf[32*8]) {
    std::fill(buf, buf + 32*8, 0u);
    size_t line = 0, col = 0;              // col = 0..31
    for (unsigned char ch : s) {
        if (line >= 32) break;
        size_t word = line * 8 + (col >> 2);        // which uint32 holds this char
        unsigned shift = (col & 3) * 8;            // byte offset in word
        buf[word] |= (uint32_t)ch << shift;
        if (ch == '\n') { ++line; col = 0; continue; }
        if (++col >= 32) { ++line; col = 0; }
    }
    // count used lines
    size_t used = 0;
    for (; used < 32; ++used) {
        bool any = false;
        for (size_t w = 0; w < 8; ++w) if (buf[used*8 + w]) { any = true; break; }
        if (!any) break;
    }
    return used;
}

}

static inline double ema_smoothing(double x, std::string const& tag) {
    static std::unordered_map<std::string, double> smoothedValues;

    constexpr double alpha = 0.01;
    if (smoothedValues.find(tag) == smoothedValues.end()) {
        smoothedValues[tag] = x;
    } else {
        if(smoothedValues[tag] == INFINITY) smoothedValues[tag] = x;
        smoothedValues[tag] = (1.0 - alpha) * smoothedValues[tag] + alpha * x;
    }

    return smoothedValues[tag];
}

static inline std::unordered_map<std::string, size_t> ioBytesPerCategory{};

inline std::string memSizeMB(std::string ioCategory, size_t count, size_t instance_bytes) {
    size_t bytes = (count * instance_bytes);
    if(ioCategory != " ")  {
        if(ioBytesPerCategory.find(ioCategory) == ioBytesPerCategory.end()) ioBytesPerCategory[ioCategory] = 0;
        ioBytesPerCategory[ioCategory] += bytes;
    }
    std::stringstream ss;
    ss << " ~" << ioCategory << ": " << std::setw(6) << std::to_string(bytes / 1024 / 1024) + "MB";
    return ss.str();
}

inline void generateDebugString(
    std::unordered_map<std::string, uint32_t> const& us_timings,
    GpuProfilingStruct const& profilingStruct,
    glm::vec3 const& cameraPosParsec, uint32_t targetBuf[32*8]) {
    ioBytesPerCategory.clear();

    std::stringstream ss;

    assert(us_timings.size() < 10);
    for(auto const& [name, time] : us_timings) {
        ss << std::setw(16) << std::left << name << " " << std::right << std::setw(6) << ema_smoothing(us_timings.at(name), "us_timing_" + name) << '\n';
    }

    ss << "rawStars: " << std::setw(9) << profilingStruct.starsDrawn  << '\n';
    ss << "s.total:  " << std::setw(9) << profilingStruct.starsTotal  <<'\n';
    ss << "chunks:   " << std::setw(5) << profilingStruct.chunksDrawn << '\n';
    ss << "tiles:    " << std::setw(5) << profilingStruct.tilesDrawn  << '\n';
    ss << "lds writes: " << std::setw(5) << profilingStruct.writesShared / 1'000'000u << std::setw(0) << "mil\n";
    ss << "glob writes:" << std::setw(5) << profilingStruct.writesGlobalDirect / 1'000'000u << std::setw(0) << "mil\n";
    double ratio = static_cast<double>(profilingStruct.writesShared) / static_cast<double>(profilingStruct.writesGlobalDirect);
    ss << "lds/glob:   " << std::setw(6) << std::setprecision(6) << std::setfill(' ') << ratio << '\n';

    assert(us_timings.find("raster") != us_timings.end());
    double microSecondsPer1M = static_cast<double>(us_timings.at("raster")) / (static_cast<double>(profilingStruct.starsTotal) / 1'000'000.0);
    ss << "per. 1m rawStars: " << std::setw(6) << ema_smoothing(microSecondsPer1M, "usPerStar") << '\n';
    ss << "job count: " << std::setw(6) << profilingStruct.jobCount << '\n';
    ss << "camera x: " << std::setw(10) << cameraPosParsec.x << '\n';
    ss << "camera y: " << std::setw(10) << cameraPosParsec.y << '\n';
    ss << "camera z: " << std::setw(10) << cameraPosParsec.z << '\n';

    ss << "rGlobalStars: " << memSizeMB("rG", profilingStruct.starsTotal, 8) << '\n';
    ss << "wSharedClear: " << memSizeMB("wS", profilingStruct.writesSharedClear, 8) << '\n';
    ss << "wShared:      " << memSizeMB("wS", profilingStruct.writesShared, 8) << '\n';
    ss << "wGlobalDirect:" << memSizeMB("wG", profilingStruct.writesGlobalDirect, 8) << '\n';
    ss << "wGlobalFlush: " << memSizeMB("wG", profilingStruct.writesGlobalFlush, 8) << '\n';

    double const rastertimeMS = static_cast<double>(us_timings.at("raster")) / 1000.0;
    double mbSum = 0.0;
    for(auto const& [category, bytes] : ioBytesPerCategory) {
        double const megabytes = bytes / (1024.0*1024.0);
        double const bytesPerStar = static_cast<double>(bytes) / profilingStruct.starsDrawn;
        mbSum += megabytes;
        ss << category << " " << std::setw(8) << bytesPerStar << '\n';
    }
    ss << "MB per 4ms:   " << std::setw(8) << ema_smoothing(mbSum / rastertimeMS, "mbp4ms") * 4.0 << '\n';
    ss << "bytes/star:  " << std::setw(8) << ema_smoothing(mbSum / profilingStruct.starsDrawn * 1024.0 * 1024.0, "bpstar") << '\n';
    ss << "us/megabyte: " << std::setw(8) << ema_smoothing(rastertimeMS * 1000.0 / mbSum, "uspmb") << '\n';


    std::string const result_str = ss.str();

    //std::cout << result_str << std::endl;

    assert(result_str.size() < 32 * 32); //TODO: use named constants for str buffer size

    fillTextBufferPacked(result_str, targetBuf);
}

}