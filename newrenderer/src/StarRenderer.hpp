#pragma once

#include "gl/SSBO.hpp"
#include <vector>

#include "gl/gl_context.hpp"
#include <glm/glm.hpp>
#include "gl/GpuTexture.hpp"
#include "preprocessing/gpu_datatypes.hpp"

#include <RawStar.hpp>

namespace newstar {

// Host calls this once after making its GL context current,
// before constructing any newstar objects.
void initialize();

class StarRenderer {
public:
    //TODO add chunk size as test parameter (limit not on distance in chunks, but in parsecs)
    StarRenderer(glm::uvec2 const& screenSize); //TODO: pass this to renderer as well, handle necessary texture update there
    void preprocessStars(std::vector<RawStar> const& rawStars);
    void prepareGpuBuffers();

    ~StarRenderer();

    void run(
        glm::mat4 modelViewMatrix,
        glm::mat4 projectionMatrix,
        float luminanceMultiplicator,
        bool hdrEnabled,
        std::array<bool, 512> const& keys_just_pressed,
        std::array<bool, 512> const& keys_down
    );

private:
    glm::uvec2 const mScreenSize; //TODO: handle viewport resizing

private:
    GLuint mClearProgram;
    GLuint mDrawListClearProgram;
    GLuint mDrawListProgram;
    GLuint mRasterProgram;
    GLuint mDebugStatsProgram;
    GLuint mDebugVisualsProgram;

    GLuint mScreenQuadProgram;

    GLuint mScreenQuadVAOHandle;

private: //framebuffers
    GpuTexture mAccumulationTexture; //for accumulating interleaved 32bit luminance-weighted temperature + 32 bit luminance
    GpuTexture mFrameTexture;
private:
    GLuint mGpuJobsSSBO;
    GLuint mGpuDispatchHeaderSSBO;
    GLuint mGpuAccumSSBO;
    SSBO mGpuChunkMetaSSBO;
    SSBO mGpuBatchRowsFlatSSBO;
    SSBO mGpuBatchRowsHighPrecisionFlat;

    GLuint mGpuProfilingSSBO;
    GpuProfilingStruct mProfilingStruct{};

private:
    size_t uChunkCount;

private:
    glm::uvec2 const mTileCount;
    size_t const mTileCount_flat;

private:
    void recompileDrawListProgram();
    void recompileRasterProgram();

    struct Config {
        glm::uvec2 screenResolution;
        size_t tileSizePx = 64;

        struct ConfigDrawlist {
            //drawlist-------------
            uint32_t HIGH_LOD_PX_THRESHOLD = 200;
            bool DEBUG_GRID_OVERLAY = false;
            bool DEBUG_VISUALIZE_BINNING = false;
            bool DEBUG_CULLING_MINIMAP = false;
            bool DEBUG_VISUALIZE_LOD_RADIUS = false;
            //---------------------

            bool operator==(const ConfigDrawlist& other) const {
            return HIGH_LOD_PX_THRESHOLD == other.HIGH_LOD_PX_THRESHOLD &&
                   DEBUG_GRID_OVERLAY == other.DEBUG_GRID_OVERLAY &&
                   DEBUG_VISUALIZE_BINNING == other.DEBUG_VISUALIZE_BINNING &&
                   DEBUG_CULLING_MINIMAP == other.DEBUG_CULLING_MINIMAP &&
                   DEBUG_VISUALIZE_LOD_RADIUS == other.DEBUG_VISUALIZE_LOD_RADIUS;
            }

            bool operator!=(const ConfigDrawlist& other) const {
                return !(*this == other);
            }
            
        } drawlist;
        
        struct ConfigRaster {
            bool RASTERIZE_USE_TILE_ACCUMULATION = true;
            bool WRITE_PROFILING_COUNTERS = true;
            bool ENABLE_LUMI_THRESHOLD = false;
            bool operator==(const ConfigRaster& other) const {
                return RASTERIZE_USE_TILE_ACCUMULATION == other.RASTERIZE_USE_TILE_ACCUMULATION &&
                    WRITE_PROFILING_COUNTERS == other.WRITE_PROFILING_COUNTERS &&
                    ENABLE_LUMI_THRESHOLD == other.ENABLE_LUMI_THRESHOLD
                    ;
            }

            bool operator!=(const ConfigRaster& other) const {
                return !(*this == other);
            }
        } raster;
    } m_config;
};

}