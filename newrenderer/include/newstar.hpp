#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <RawStar.hpp>

void newstar_init(glm::uvec2 screenSize);
void newstar_setdata(std::vector<RawStar> const& rawStars);
void newstar_render(
    glm::mat4 modelViewMatrix,
    glm::mat4 projectionMatrix,
    float luminanceMultiplicator,
    bool hdrEnabled,
    std::array<bool, 512> const& keys_just_pressed = {false},
    std::array<bool, 512> const& keys_down = {false});

void newstar_delete();