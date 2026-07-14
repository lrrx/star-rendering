#pragma once

#include "../../IComputeProgram.hpp"
#include "../../gl/ssbo.hpp"
#include <vector>
#include <string>

class VRAMWriteCompute : public IComputeProgram {
public:
    VRAMWriteCompute::VRAMWriteCompute(CameraController const& camera, Window& window);
    ~VRAMWriteCompute();

    void updateKeys() override;
    void setUniforms() override;
    void run() override;
    GLuint getFrameTexture() override;

private:
    GLuint mComputeTexture;
    GLuint mFrameTexture;

    GLuint mClearProgram;
    GLuint mRasterProgram;
};