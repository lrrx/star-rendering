#include "VRAMWriteCompute.hpp"

#include <iostream>
#include <glm/glm.hpp>
#include <map>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include "../../camera/camera_controller.hpp"
#include "../../util/util.hpp"
#include "../../util/shader_utils.hpp"

#include "../../stars/stars.hpp"
#include <algorithm>
#include <iomanip>
#include <unordered_set>
#include "../../util/util.hpp"

namespace {

GLuint createTexture(glm::uvec2 size) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size.x, size.y, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

}

constexpr size_t TEX_SCALE = 1;

VRAMWriteCompute::VRAMWriteCompute(CameraController const& camera, Window& window)
: IComputeProgram(camera, window),
mFrameTexture{createTexture(mScreenSize)},
mComputeTexture{createTexture(glm::uvec2(1920 * TEX_SCALE, 1080 * TEX_SCALE))}
{
    //no fancy setup since we dont need to set up SSBO data for reading
    //only shaders
    
    //TODO: leave out clear shader step or split up measurements?
    //mClearProgram = createComputeProgramFromFile("src/shaders/compute_clear.comp", 64);
    //mRasterProgram = createComputeProgramFromFile("src/experiments/vram-limits/test_atomic_writes.comp", 32);
}

void VRAMWriteCompute::updateKeys() {

};

void VRAMWriteCompute::setUniforms() {
 
}

void VRAMWriteCompute::run() {
    //TODO: use glProgramUniform instead, so that we dont have to pass the matrices trough class members

    glBindImageTexture(0, mComputeTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
    glm::uvec2 size = glm::uvec2(1920 * TEX_SCALE, 1080 * TEX_SCALE);

    //clear compute screen buffer
    glUseProgram(mClearProgram);
    glUniform2i(glGetUniformLocation(mClearProgram, "uResolution"), size.x, size.y);
    glDispatchCompute((size.x + 7) / 8, (size.y + 7) / 8, 1);

    //run main rasterization
    glUseProgram(mRasterProgram);
    glUniform2i(glGetUniformLocation(mRasterProgram, "uResolution"), size.x, size.y);
    glDispatchCompute(30, 17, 1);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

GLuint VRAMWriteCompute::getFrameTexture() {
    return mComputeTexture;
}

VRAMWriteCompute::~VRAMWriteCompute() {
    glDeleteTextures(1, &mComputeTexture);
    glDeleteTextures(1, &mFrameTexture);

    glDeleteProgram(mClearProgram);
    glDeleteProgram(mRasterProgram);
}