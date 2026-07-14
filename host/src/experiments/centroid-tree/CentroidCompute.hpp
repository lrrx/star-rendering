#pragma once

#include "../../IComputeProgram.hpp"
#include "../../gl/ssbo.hpp"
#include <vector>
#include <string>

class CentroidCompute : public IComputeProgram {
public:
    CentroidCompute::CentroidCompute(CameraController const& camera, Window& window, size_t const CHUNK_SIZE, size_t const WARP_SIZE);
    ~CentroidCompute();

    void updateKeys() override;
    void setUniforms() override;
    void run() override;
private:
    size_t mVisibleCellCount;

    GLuint mGeneratorProgram;

    GLuint mClearProgram;
    GLuint mRasterProgram;

    SSBO mOriginalSSBO;
    SSBO mNearSSBO;
    SSBO mFarSSBO;

    size_t const mWarpSize;
};