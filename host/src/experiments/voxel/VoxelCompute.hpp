#pragma once

#include "../../IComputeProgram.hpp"
#include "../../gl/ssbo.hpp"
#include <vector>
#include <string>

class VoxelCompute : public IComputeProgram {
public:
    VoxelCompute::VoxelCompute(CameraController const& camera, Window& window, size_t const CHUNK_SIZE, size_t const WARP_SIZE);
    ~VoxelCompute();

    void updateKeys() override;
    void setUniforms() override;
    void run() override;
private:
    size_t mVisibleCellCount;

    GLuint mClearProgram;
    GLuint mRasterProgram;

    SSBO mCellSSBO;
    SSBO mPointSSBO;

    size_t const mWarpSize;

private: //Debug mode keybinds

    std::vector<std::pair<std::string, int>> const debugTogglesKeymap = 
    std::vector<std::pair<std::string, int>>{
        {"USE_FLAT_TOP_PROJECTION", GLFW_KEY_1},
        {"DEBUG_CELL_INFOS", GLFW_KEY_2},
        {"DEBUG_VOXEL_CENTERS", GLFW_KEY_3},
        {"DEBUG_VOXEL_STAR_COUNTS", GLFW_KEY_4},
        {"DEBUG_VOXEL_OUTLINES", GLFW_KEY_5},
        {"DEBUG_BINARY_STRIPES", GLFW_KEY_6},
        {"DEBUG_ISOMETRIC_MINIMAP", GLFW_KEY_7},
        {"DEBUG_ENABLED", GLFW_KEY_0}
    };

    std::vector<bool> keyStates = std::vector<bool>(8 , false);
};