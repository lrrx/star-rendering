#include "better_galaxy_gen.hpp"
#include "SimplexNoise.h"

#include <random>
#include <numbers>

// ---------- Random number helpers ------
namespace {

float rand_uniform()
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<float> dist{0.0f, 1.0f};
    return dist(rng);
}
  
float rand_gaussian()
{
    static thread_local bool have_spare = false;
    static thread_local float spare;

    if (have_spare) {
        have_spare = false;
        return spare;
    }

    float u, v, s;
    do {
        u = rand_uniform() * 2.0f - 1.0f;
        v = rand_uniform() * 2.0f - 1.0f;
        s = u * u + v * v;
    } while (s >= 1.0f || s == 0.0f);

    s = std::sqrt(-2.0f * std::log(s) / s);
    have_spare = true;
    spare = v * s;
    return u * s;
}

constexpr float PI = 3.14159265358979323846f;
}

// ---------- Milky Way Generator with Proper Spiral and Stellar Clouds ------
glm::vec4 generateBetterGalaxyStar()
{    
    float scaleLength = 50.f;   // Disc scale length (kpc) - default ~5.0
    float maxRadius = 10.f;     // Outer cutoff radius (kpc) - default ~20.0
    float scaleHeight = 1.f;    // Vertical scale height (kpc) - default ~0.2
    float spiralArms = 6.0f;    // Number of spiral arms - default 3
    float armDensity = 150.f;   // How dense the arms are - default 1.5  

    // 1. Base radial distance – exponential disc distribution
    float r = -scaleLength * std::log(1.0f - rand_uniform());
    
    // Reject points beyond outer cutoff
    while (r > maxRadius) {
        r = -scaleLength * std::log(1.0f - rand_uniform());
    }

    // 2. Spiral arm position calculation
    const float phi = rand_uniform() * 2.0f * PI;
    
    // 3. Create SimplexNoise for structure generation
    // Scale noise based on radius and arm count
    constexpr float noiseScale = 15.0f;
    static thread_local SimplexNoise starNoise(1.0f / noiseScale, 1.0f, 2.0f, 0.5f);
    
    // Calculate density modifier based on spiral arm structure
    // Stars concentrate along arm paths defined by angle + radius
    float armPosition = phi * spiralArms - r / (scaleLength * 3.0f);
    float armDensityVal = starNoise.noise(armPosition, r * 0.1f, 0.0f);
    
    // Normalize to [0, 2] range for density variation (clumps outside center)
    float densityModifier = 0.5f + 1.5f * armDensityVal;
    
    // 4. Height above the mid-plane – Gaussian with clumping variation
    float zHeight = rand_gaussian() * scaleHeight;
    
    // Increase height variation along arms (thicker in arms)
    if (armDensityVal > 0.3f && armDensityVal < 1.0f) {
        // Stars outside center are more spread vertically in arms
        float outerFactor = 1.0f + (r / maxRadius) * 0.5f;
        zHeight *= outerFactor;
    }

    // 5. Convert to Cartesian coordinates
    float x = r * std::cos(phi);
    float y = r * std::sin(phi);
    float z = zHeight;

    // 6. Apply density modifier to position for clumping effect
    // More clumping further from center (stellar clouds)
    float cloudDensity = 1.0f + armDensityVal * densityModifier;
    
    if (r > maxRadius * 0.3f) {
        // Stellar clouds more prominent outside central 30% of galaxy
        x *= cloudDensity;
        y *= cloudDensity;
        z *= cloudDensity;
    }

    // 7. Central bulge for Milky Way center
    float distFromCenter = std::sqrt(x * x + y * y);
    if (distFromCenter < maxRadius * 0.15f) {
        // Denser central region
        float bulgeFactor = 1.0f + 0.3f * (maxRadius * 0.15f - distFromCenter) / (maxRadius * 0.15f);
        x *= bulgeFactor;
        y *= bulgeFactor;
        z *= bulgeFactor * 2.0f; // Slightly thicker bulge
    }

    return glm::vec4(x, z, y, 0.0f);
}