#include <newstar.hpp>

#include <vector>
#include <RawStar.hpp>

#include <memory>
#include "StarRenderer.hpp"

std::unique_ptr<newstar::StarRenderer> starRenderer;

void newstar_init(glm::uvec2 screenSize) {
    //init gl function pointers
    newstar::initialize();

    starRenderer = std::make_unique<newstar::StarRenderer>(screenSize);
}

void newstar_setdata(std::vector<RawStar> const& rawStars) {
    starRenderer->preprocessStars(rawStars);
    starRenderer->prepareGpuBuffers();
}

void newstar_render(
    glm::mat4 modelViewMatrix,
    glm::mat4 projectionMatrix,
    float luminanceMultiplicator,
    bool hdrEnabled,
    std::array<bool, 512> const& keys_just_pressed,
    std::array<bool, 512> const& keys_down
) {
    starRenderer->run(
        modelViewMatrix,
        projectionMatrix,
        luminanceMultiplicator,
        hdrEnabled,
        keys_just_pressed,
        keys_down);
}

void newstar_delete() {
    starRenderer.reset(); //trigger destructor through unique_ptr reset
}