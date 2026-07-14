#pragma once

#include "../../IComputeProgram.hpp"
#include "../../gl/ssbo.hpp"
#include <vector>
#include <string>

class TilebasedCompute : public IComputeProgram {
public:
    //TODO add chunk size as test parameter (limit not on distance in chunks, but in parsecs)
    TilebasedCompute::TilebasedCompute(CameraController const& camera, Window& window);
    ~TilebasedCompute();

    void updateKeys() override;
    void setUniforms() override;
    void run() override;
    GLuint getFrameTexture() override;
private:
    size_t mVisibleCellCount;

    GLuint mClearProgram;
    GLuint mRasterProgram;

    GLuint mFrameTexture;
    
private:
    SSBO mGpuChunkMetaSSBO;
    SSBO mGpuBatchesFlatSSBO;
};
    