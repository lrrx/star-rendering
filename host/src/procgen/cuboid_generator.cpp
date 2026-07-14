#include "cuboid_generator.hpp"

#include <random>

float rand_float(
    float const min = 0.f,
    float const max = 1.f)
{
    static thread_local std::mt19937 gen(42);
    static thread_local std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(gen) * (max - min) + min;
}

float rand_float_gaussian()
{
    //static std::random_device rd{};
    static std::mt19937 gen{42};
    static std::normal_distribution d{0.0, 10.0};

    // Draw a sample from the normal distribution and round it to an integer.
    return d(gen);
}

glm::vec4 random_cuboid_position()
{
    float const POINT_RADIUS = 2.f;

    return glm::vec4{
        rand_float(0, POINT_RADIUS),
        rand_float(0, POINT_RADIUS),//rand_float(-POINT_RADIUS, POINT_RADIUS),// * rand_float_gaussian(),
        rand_float(0, POINT_RADIUS),
        0.0
    };
}