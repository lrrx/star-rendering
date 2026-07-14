#pragma once

#include <glm/vec2.hpp>
#include "camera/camera_controller.hpp"

class IComputeProgram {
public:
    IComputeProgram(CameraController const& camera, Window& window);
    virtual ~IComputeProgram() = default;

    virtual void updateKeys() = 0;
    virtual void setUniforms() = 0;
    virtual void run() = 0;
    virtual GLuint getFrameTexture() = 0;

protected:
    CameraController const& mCamera;
    Window& mWindow;
    glm::uvec2 mScreenSize;
};