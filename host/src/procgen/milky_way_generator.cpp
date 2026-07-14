#include "milky_way_generator.hpp"

#include <random>
#include <numbers>

// ---------- Random number helpers ------------------------------------
float rand_uniform()
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<float> dist{0.0f, 1.0f};
    return dist(rng);
}

// Box–Muller transform – standard normal (mean = 0, sd = 1)
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
        u = rand_uniform() * 2.0f - 1.0f;   // in [-1,1)
        v = rand_uniform() * 2.0f - 1.0f;   // in [-1,1)
        s = u * u + v * v;
    } while (s >= 1.0f || s == 0.0f);

    s = std::sqrt(-2.0f * std::log(s) / s);
    have_spare = true;
    spare      = v * s;
    return u * s;
}

// ---------- Milky‑Way disc generator ---------------------------------
/**
 * @brief Generate one star position that follows a Milky‑Way disc.
 *
 * @param scaleLength  Exponential scale length of the disc (kpc).  Typical
 *                     value ≈ 5.0 kpc.
 * @param maxRadius    Maximum radial cutoff (kpc).  Stars beyond this
 *                     radius are discarded and regenerated.
 * @param scaleHeight  Vertical scale height of the disc (kpc).  Typical
 *                     value ≈ 0.2 kpc.
 * @return glm::vec4  Cartesian position (x, y, z, 0.0f).
 */
glm::vec4 generateMilkyWayStar()
{
    float scaleLength = 5.0f;      // disc scale length
    float maxRadius   = 31.0f;     // outer cutoff
    float scaleHeight = 2.f;     // disc thickness

    // 1.  Radial distance – exponential law ρ(r) ∝ exp(−r/scaleLength)
    float r = -scaleLength * std::log(1.0f - rand_uniform());
    // reject points beyond the outer cutoff
    while (r > maxRadius) {
        r = -scaleLength * std::log(1.0f - rand_uniform());
    }

    // 2.  Azimuthal angle – uniform in [0, 2π)
    const float phi = rand_uniform() * 2.0f * float(3.1415926);

    // 3.  Height above the mid‑plane – Gaussian about z = 0
    const float z = rand_gaussian() * scaleHeight;

    // 4.  Convert to Cartesian coordinates (disc lies in the xy‑plane)
    const float x = r * std::cos(phi);
    const float y = r * std::sin(phi);

    return glm::vec4(x, z, y, 0.0f);   // w = 0.0f (unused)
}
