#pragma once

#include <vector>
#include <glm/glm.hpp>

#include <cmath>
#include <limits>
#include <cstdint>
#include <algorithm>

#include <RawStar.hpp>

#include "gpu_datatypes.hpp"
#include "util/util.hpp"
#include <iostream>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/string_cast.hpp>

struct TemperatureColor {
    uint8_t l_warm; //maybe encode in exponential for better HDR depth?
    uint8_t l_cold; //maybe encode in exponential for better HDR depth?
};

inline static uint32_t pack10(glm::u32vec3 const& v) {
    return ((v.x & 0x3FFu) << 20) | ((v.y & 0x3FFu) << 10) | (v.z & 0x3FFu);
}

inline glm::u32vec3 quantize(glm::vec3 v) {
    glm::vec3 pos = v;
    assert(0.0f <= glm::compMin(pos) && glm::compMax(pos) < 1.0f);

    static float const maxVal = std::nextafter(1.0f, 0.0f);

    glm::vec3 clamped = glm::clamp(pos, 0.0f, maxVal);

    const uint32_t FLOAT_BITS = static_cast<uint32_t>(std::numeric_limits<float>::digits); //normally 24 bits (23+1 implicit)
    return glm::u32vec3(clamped * float(1u << FLOAT_BITS));
}

struct QuantizedStar { //quantized variying resolution representation
public:
    //TODO: accept double precision output, otherwise the last bits of highp are just useless numeric noise (23 bit mantissa vs. 30 bits per axis)
    //idea from the 2022 paper: "Software Rasterization of 2 Billion Points in Real Time" by MARKUS SCHÜTZ et. al

    //TODO: render higher precision stars not using distance, but using screenspace size
    //store midp + highp switches in chunk metadata (2 bit field 00 = reserved, 01 = lowp, 10 = lowp+midp, 11 = lowp+midp+highp)
    //data row adresses calculated implicitly from this + existing chunk mem address
    //for good cache coherence -> should extra precision rows be interleaved or stored at different buffer?
    //
    struct MultiPrecisionCoordinate {
        MultiPrecisionCoordinate(glm::vec3 const& inputPos) {
            auto vq = quantize(inputPos);

            std::array<uint32_t * const, 3> targets = {&lowp, &midp, &highp};
            std::array<size_t, 3> shifts = {14, 4, 0};
            std::array<uint32_t, 3> masks = {0x3FF, 0x3FF, 0xF};

            for(size_t i = 0; i < 3; i++) {
                uint32_t& target = *(targets[i]);
                size_t const shift = shifts[i];
                size_t const mask = masks[i];
                glm::u32vec3 toPack(
                    (vq.x >> shift) & mask,
                    (vq.y >> shift) & mask,
                    (vq.z >> shift) & mask
                );

                target = pack10(toPack);
            }
        }

        uint32_t lowp{};
        uint32_t midp{};
        uint32_t highp{};
    };

    MultiPrecisionCoordinate coord;

    float mag = 0.0;
    float temp = 0.0;

    QuantizedStar(glm::vec3 const& localPosition, float magnitude, float temperature
    ) :
    coord{localPosition},
    mag{magnitude},
    temp{temperature} {}

    QuantizedStar& operator+=(QuantizedStar const& other) {
        float newMag = mag + other.mag;
        if (newMag > 0.0f)
            temp = (mag * temp + other.mag * other.temp) / newMag;
        mag = newMag;
        return *this;
    }
};

struct RawChunk {
    uint32_t globalPosEncoded = 0; //packed world position
    glm::ivec3 globalPos{}; //non-packed world position, for easier access
    std::vector<RawStar> stars{};
    
    //auxilliary statistical counters
    uint32_t merged_count = 0;
    uint32_t original_count = 0;
};

struct QuantizedChunk {
    QuantizedChunk(std::vector<RawStar> const& stars, size_t starCount, uint32_t _globalPosEncoded, uint8_t size);

    uint32_t globalPosEncoded;
    std::vector<QuantizedStar> quantizedStars;
    uint8_t size = 1;

    void gpuSerialize(
        std::vector<GpuBatchRow>& batchRowsOut,
        std::vector<GpuBatchRow>& highPrecisionBatchRowsOut,
        std::vector<GpuChunkMeta>& chunkMetaOut
    );
};
