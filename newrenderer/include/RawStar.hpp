#pragma once

#include <glm/glm.hpp>

// RawStar structure matching Cosmoscout Stars::mStars structure
struct RawStar {
    float mMagnitude;               // Apparent magnitude
    float mTEff;                    // Effective temperature (K)
    glm::highp_f64vec3 mPosition;     // XYZ position in parsecs, with f32 precision //TODO: switch to high precision coordinates
};