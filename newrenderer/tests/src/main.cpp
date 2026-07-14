#include <iostream>
#include <cmath>
#include <cassert>
#include <vector>
#include <string>
#include <preprocessing/datatypes.hpp>
#include <glm/glm.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/string_cast.hpp>

// ── helpers ──────────────────────────────────────────────────────────

glm::uvec3 unpackVec10(uint32_t packed) {
    return glm::uvec3(
        (packed >> 20) & 0x3FF,
        (packed >> 10) & 0x3FF,
        (packed >>  0) & 0x3FF
    );
}

glm::dvec3 reconstructFull(QuantizedStar::MultiPrecisionCoordinate const& c) {
    glm::u64vec3 r = glm::u64vec3(unpackVec10(c.lowp));
    r <<= 10;
    r += glm::u64vec3(unpackVec10(c.midp));
    r <<= 4;
    r += glm::u64vec3(unpackVec10(c.highp));
    return glm::dvec3(r) / double(1u << 24);
}

glm::dvec3 reconstructLowpOnly(QuantizedStar::MultiPrecisionCoordinate const& c) {
    glm::u64vec3 r = glm::u64vec3(unpackVec10(c.lowp));
    return glm::dvec3(r) / double(1u << 10);  // lowp covers top 10 of 24 bits
}

glm::dvec3 reconstructLowpMidp(QuantizedStar::MultiPrecisionCoordinate const& c) {
    glm::u64vec3 r = glm::u64vec3(unpackVec10(c.lowp));
    r <<= 10;
    r += glm::u64vec3(unpackVec10(c.midp));
    return glm::dvec3(r) / double(1u << 20);  // lowp+midp = top 20 of 24 bits
}

int tests_passed = 0;
int tests_failed = 0;

void check(bool cond, std::string const& name) {
    if (cond) {
        std::cout << "  PASS: " << name << std::endl;
        tests_passed++;
    } else {
        std::cout << "  FAIL: " << name << std::endl;
        tests_failed++;
    }
}

// ── tests ────────────────────────────────────────────────────────────

void test_roundtrip_exact() {
    std::cout << "\n=== Round-trip reconstruction ===" << std::endl;

    struct Case { glm::vec3 v; std::string label; };
    std::vector<Case> cases = {
        {{1.f / 512.f, 0.1f, 0.2f},            "mixed values"},
        {{0.0f, 0.0f, 0.0f},                    "origin"},
        {{0.5f, 0.5f, 0.5f},                    "center"},
        {{0.25f, 0.125f, 0.0625f},              "exact powers of two"},
        {{0.999f, 0.999f, 0.999f},              "near one"},
        {{1e-6f, 1e-6f, 1e-6f},                 "near zero"},
        {{0.0f, 0.999f, 0.5f},                  "mixed extremes"},
        {{1.f / 3.f, 1.f / 7.f, 1.f / 13.f},   "irrational-ish fractions"},
        {{0.123456f, 0.654321f, 0.789012f},     "arbitrary"},
    };

    for (auto const& c : cases) {
        auto star = QuantizedStar(c.v, 1.0, 1000.0);
        glm::dvec3 recon = reconstructFull(star.coord);
        // quantize truncates, so recon <= input.
        // max error is 1 LSB = 1/2^24 ≈ 5.96e-8
        double maxErr = 1.0 / double(1u << 24);
        glm::dvec3 diff = glm::dvec3(c.v) - recon;
        bool ok = diff.x >= 0.0 && diff.x < maxErr
               && diff.y >= 0.0 && diff.y < maxErr
               && diff.z >= 0.0 && diff.z < maxErr;
        check(ok, "roundtrip: " + c.label);
        if (!ok) {
            std::cout << "    input: " << glm::to_string(c.v) << std::endl;
            std::cout << "    recon: " << glm::to_string(recon) << std::endl;
            std::cout << "    diff:  " << glm::to_string(diff) << std::endl;
        }
    }
}

void test_progressive_precision() {
    std::cout << "\n=== Progressive refinement ===" << std::endl;

    glm::vec3 input(0.123456f, 0.654321f, 0.789012f);
    auto star = QuantizedStar(input, 1.0, 1000.0);

    glm::dvec3 rLow  = reconstructLowpOnly(star.coord);
    glm::dvec3 rMid  = reconstructLowpMidp(star.coord);
    glm::dvec3 rFull = reconstructFull(star.coord);
    glm::dvec3 dv    = glm::dvec3(input);

    // each level should be closer to the input than the previous
    double errLow  = glm::length(dv - rLow);
    double errMid  = glm::length(dv - rMid);
    double errFull = glm::length(dv - rFull);

    std::cout << "    lowp  error: " << errLow  << std::endl;
    std::cout << "    mid   error: " << errMid  << std::endl;
    std::cout << "    full  error: " << errFull << std::endl;

    check(errLow  > errMid,  "midp improves on lowp");
    check(errMid  > errFull, "highp improves on midp");

    // lowp alone: max error is 1/2^10 ≈ 0.001 per axis
    double lowpBound = std::sqrt(3.0) / double(1u << 10);
    check(errLow < lowpBound, "lowp error within 10-bit bound");

    // lowp+midp: max error is 1/2^20 per axis
    double midpBound = std::sqrt(3.0) / double(1u << 20);
    check(errMid < midpBound, "midp error within 20-bit bound");

    // full: max error is 1/2^24 per axis
    double fullBound = std::sqrt(3.0) / double(1u << 24);
    check(errFull < fullBound, "full error within 24-bit bound");
}

void test_bit_layout_no_overlap() {
    std::cout << "\n=== Bit layout: no overlap, no gap ===" << std::endl;

    // Use a value whose 24-bit quantization has bits set everywhere
    // 0.987654f * 2^24 = 16570798 = 0xFCED0E
    // binary: 1111 1100 1110 1101 0000 1110
    //         [  lowp 10  ] [ midp 10  ] [h4]
    glm::vec3 input(0.987654f, 0.456789f, 0.111111f);
    auto star = QuantizedStar(input, 1.0, 1000.0);

    auto lo = unpackVec10(star.coord.lowp);
    auto mi = unpackVec10(star.coord.midp);
    auto hi = unpackVec10(star.coord.highp);

    // reassemble each axis independently and compare to quantize() output
    auto vq = quantize(input);
    for (int axis = 0; axis < 3; axis++) {
        uint32_t reassembled = (lo[axis] << 14) | (mi[axis] << 4) | hi[axis];
        check(reassembled == vq[axis],
              "axis " + std::to_string(axis) + " bit reassembly matches quantize()");
        if (reassembled != vq[axis]) {
            std::cout << "    quantize: " << vq[axis]
                      << " reassembled: " << reassembled << std::endl;
        }
    }
}

void test_highp_field_bounded() {
    std::cout << "\n=== highp uses only 4 bits ===" << std::endl;

    std::vector<glm::vec3> inputs = {
        {0.0f, 0.0f, 0.0f},
        {0.999f, 0.999f, 0.999f},
        {0.5f, 0.25f, 0.75f},
        {1.f/3.f, 2.f/3.f, 1.f/7.f},
    };

    bool allOk = true;
    for (auto const& v : inputs) {
        auto star = QuantizedStar(v, 1.0, 1000.0);
        auto hi = unpackVec10(star.coord.highp);
        if (hi.x > 15 || hi.y > 15 || hi.z > 15) {
            std::cout << "    OVERFLOW: " << glm::to_string(hi)
                      << " for input " << glm::to_string(v) << std::endl;
            allOk = false;
        }
    }
    check(allOk, "all highp components fit in 4 bits (< 16)");
}

void test_zero_and_near_one_boundaries() {
    std::cout << "\n=== Boundary values ===" << std::endl;

    // exact zero should quantize to all-zero
    {
        auto star = QuantizedStar(glm::vec3(0.0f), 1.0, 1000.0);
        auto lo = unpackVec10(star.coord.lowp);
        auto mi = unpackVec10(star.coord.midp);
        auto hi = unpackVec10(star.coord.highp);
        check(lo == glm::uvec3(0) && mi == glm::uvec3(0) && hi == glm::uvec3(0),
              "zero input → all packed words zero");
    }

    // near-one should produce the maximum quantized value (2^24 - 1 = 16777215)
    {
        float almostOne = std::nextafter(1.0f, 0.0f);
        auto star = QuantizedStar(glm::vec3(almostOne), 1.0, 1000.0);
        auto vq = quantize(glm::vec3(almostOne));
        // all three axes should be the same maximum
        check(vq.x == vq.y && vq.y == vq.z, "near-one quantizes uniformly");
        // reconstruct and verify it's close to 1.0
        glm::dvec3 recon = reconstructFull(star.coord);
        check(recon.x > 0.999 && recon.x < 1.0, "near-one reconstructs close to 1.0");
    }
}

void test_chunk_spatial_coherence() {
    std::cout << "\n=== Chunk coherence: nearby stars share lowp ===" << std::endl;

    // Stars within the same 1/1024 cell should have identical lowp
    // This is the key property for your chunk-based renderer:
    // coarse sorting by lowp groups nearby stars together.
    glm::vec3 base(0.5f, 0.5f, 0.5f);
    float cellSize = 1.0f / 1024.0f;  // 1 LSB of lowp

    auto star1 = QuantizedStar(base, 1.0, 1000.0);
    auto star2 = QuantizedStar(base + glm::vec3(cellSize * 0.1f), 1.0, 1000.0);
    auto star3 = QuantizedStar(base + glm::vec3(cellSize * 0.9f), 1.0, 1000.0);

    check(star1.coord.lowp == star2.coord.lowp,
          "stars 0.1 cells apart share lowp");
    check(star1.coord.lowp == star3.coord.lowp,
          "stars 0.9 cells apart share lowp");

    // A star one full cell away should differ in lowp
    auto starFar = QuantizedStar(base + glm::vec3(cellSize * 1.5f, 0.0f, 0.0f), 1.0, 1000.0);
    check(star1.coord.lowp != starFar.coord.lowp,
          "star 1.5 cells away has different lowp");
}

void test_symmetry() {
    std::cout << "\n=== Axis symmetry ===" << std::endl;

    // same scalar on all three axes should produce identical per-axis values
    float val = 0.314159f;
    auto star = QuantizedStar(glm::vec3(val), 1.0, 1000.0);
    auto lo = unpackVec10(star.coord.lowp);
    auto mi = unpackVec10(star.coord.midp);
    auto hi = unpackVec10(star.coord.highp);

    check(lo.x == lo.y && lo.y == lo.z, "uniform input → lowp xyz equal");
    check(mi.x == mi.y && mi.y == mi.z, "uniform input → midp xyz equal");
    check(hi.x == hi.y && hi.y == hi.z, "uniform input → highp xyz equal");
}

// ── main ─────────────────────────────────────────────────────────────

int main() {
    test_roundtrip_exact();
    test_progressive_precision();
    test_bit_layout_no_overlap();
    test_highp_field_bounded();
    test_zero_and_near_one_boundaries();
    test_chunk_spatial_coherence();
    test_symmetry();

    std::cout << "\n================================" << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}