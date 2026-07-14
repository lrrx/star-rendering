#include "StarRenderer.hpp"
#include <cstdlib>
#include <optional>
#include <string>
#include <variant>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

#include "gl/shader_utils.hpp"
#include "gl/GpuStopwatch.hpp"

#include "preprocessing/gpu_datatypes.hpp"
#include "preprocessing/preprocessing.hpp"

//#define REBUILD_CACHE

#ifdef REBUILD_CACHE
#include "stars/stars.hpp"
#endif

#include <util/util.hpp>

#include <RawStar.hpp>
#include "util/glfw_keymap.hpp"
#include "util/generate_debug_string.hpp"

namespace newstar {

void initialize() {
    int version = gladLoaderLoadGLContext(&detail::g_gl_context);
    if (!version) {
        throw std::runtime_error("newstar: gladLoaderLoadGLContext failed");
    }
    return;
}

constexpr size_t GPU_ACCUMULATION_LANES = 2;

constexpr size_t GPU_TILE_SIZE_PX = 64;

// Max chunks bundled per job. With MAX_CHUNK_ROWS = 8 on the CPU side
// (see datatypes.cpp gpuSerialize), peak rows per job = 8 * 16 = 128.
constexpr size_t GPU_MAX_JOB_CHUNKS = 10;

// Total jobs across the screen per frame (primaries + overflow). Sized so the
// SSBO is the source of truth for capacity; the shaders' MAX_JOBS define and
// the buffer allocation both derive from this.
constexpr size_t GPU_MAX_JOBS = 100000;

/*double pixel_world_space_width(double dist) {
    return 2.0 * dist / (960.0); //tan(45°) = 1
}*/ //TODO: use this to generate dynamic LOD levels, for now we assume fixed precision

void StarRenderer::recompileDrawListProgram() {
    static bool firstRunDone = false;
    
    if(!firstRunDone) {
        GL.DeleteProgram(mDrawListProgram);
    }

    firstRunDone = true;

    mDrawListProgram = createComputeProgramFromFile("drawlist.comp", {
        {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS},
        {"TILE_SIZE", GPU_TILE_SIZE_PX},
        {"NUM_PRIMARY_TILES", mTileCount_flat},
        {"TILES_X", mTileCount.x},
        {"TILES_Y", mTileCount.y},
        {"MAX_JOBS", GPU_MAX_JOBS},
        {"HIGH_LOD_PX_THRESHOLD", m_config.drawlist.HIGH_LOD_PX_THRESHOLD},
        {m_config.drawlist.DEBUG_GRID_OVERLAY ? "DEBUG_GRID_OVERLAY" : "", std::nullopt},
        {m_config.drawlist.DEBUG_VISUALIZE_BINNING ? "DEBUG_VISUALIZE_BINNING" : "", std::nullopt},
        {m_config.drawlist.DEBUG_CULLING_MINIMAP ? "DEBUG_CULLING_MINIMAP" : "", std::nullopt},
        {m_config.drawlist.DEBUG_VISUALIZE_LOD_RADIUS ? "DEBUG_VISUALIZE_LOD_RADIUS" : "", std::nullopt},
    });
}

void StarRenderer::recompileRasterProgram() {
    static bool firstRunDone = false;

    if(!firstRunDone) {
        GL.DeleteProgram(mRasterProgram);
    }

    firstRunDone = true;
    
    mRasterProgram = createComputeProgramFromFile("rasterize.comp", {
        {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS},
        {"THREAD_COUNT", GPU_THREAD_COUNT},
        {"TILE_SIZE", GPU_TILE_SIZE_PX},
        {"TILES_X", mTileCount.x},
        {"TILES_Y", mTileCount.y},
        {"MAX_JOBS", GPU_MAX_JOBS},
        {"ACCUMULATION_LANES", GPU_ACCUMULATION_LANES},
        {m_config.raster.RASTERIZE_USE_TILE_ACCUMULATION ? "RASTERIZE_USE_TILE_ACCUMULATION" : "", std::nullopt},
        {m_config.raster.WRITE_PROFILING_COUNTERS ? "WRITE_PROFILING_COUNTERS" : "", std::nullopt},
        {m_config.raster.ENABLE_LUMI_THRESHOLD ? "ENABLE_LUMI_THRESHOLD" : "", std::nullopt},
    });
}

StarRenderer::StarRenderer(glm::uvec2 const& screenSize)
: mScreenSize{screenSize},
 mAccumulationTexture{(mScreenSize * glm::uvec2(2,1) * glm::uvec2(1, GPU_ACCUMULATION_LANES)), GpuTexture::Format::R32F}, //twice as wide, interleaved 2-value storage
 mFrameTexture{(mScreenSize), GpuTexture::Format::RGBA8},
mTileCount{glm::uvec2(glm::ceil(glm::vec2(mScreenSize) / glm::vec2(GPU_TILE_SIZE_PX)))},
mTileCount_flat{mTileCount.x * mTileCount.y}
{
    mClearProgram = createComputeProgramFromFile("clear.comp", {});
    mDrawListClearProgram = createComputeProgramFromFile("drawlist_clear.comp", {
        {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS},
        {"NUM_PRIMARY_TILES", mTileCount_flat}
    });

    recompileDrawListProgram();
    recompileRasterProgram();

    mDebugStatsProgram = createComputeProgramFromFile("debugstats.comp", {});
    mDebugVisualsProgram = createComputeProgramFromFile("debugvisuals.comp", {
        {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS},
        {"NUM_PRIMARY_TILES", mTileCount_flat},
        {"TILE_SIZE", GPU_TILE_SIZE_PX}
    });

    mScreenQuadProgram = createProgramFromFiles("quad.vert", "quad.frag");
}

void StarRenderer::preprocessStars(std::vector<RawStar> const& rawStars) {
#ifdef REBUILD_CACHE
    std::vector<RawStar> rs{};
    std::cout << "reading cache" << std::endl;
    stars::readStarCache("/home/u/git/stars/data/full_gaia_stars_icrs.cache", rs, true);

    //rawStars = rs;

    std::vector<GpuChunkMeta> gpuChunkMetas;
    std::vector<uint32_t> gpuBatchRowsFlat;
    std::vector<uint32_t> gpuBatchRowsHighPrecisionFlat;

    preprocessing::run(rs, gpuChunkMetas, gpuBatchRowsFlat, gpuBatchRowsHighPrecisionFlat);    //upload data to GPU
    mGpuChunkMetaSSBO.create(gpuChunkMetas, GL_STATIC_DRAW, "/home/u/git/stars/data/chunks_galactic.bin");
    mGpuBatchRowsFlatSSBO.create(gpuBatchRowsFlat, GL_STATIC_DRAW, "/home/u/git/stars/data/rowsFlat_galactic.bin");
    mGpuBatchRowsHighPrecisionFlat.create(gpuBatchRowsHighPrecisionFlat, GL_STATIC_DRAW, "/home/u/git/stars/data/rowsHighPrecisionFlat_galactic.bin");
    //std::quick_exit(0);

#else
    mGpuChunkMetaSSBO.loadFromFile<GpuChunkMeta>("/home/u/git/stars/data/chunks_galactic.bin", GL_STATIC_DRAW);
    mGpuBatchRowsFlatSSBO.loadFromFile<uint32_t>("/home/u/git/stars/data/rowsFlat_galactic.bin", GL_STATIC_DRAW);
    mGpuBatchRowsHighPrecisionFlat.loadFromFile<uint32_t>("/home/u/git/stars/data/rowsHighPrecisionFlat_galactic.bin", GL_STATIC_DRAW);
#endif
    uChunkCount = mGpuChunkMetaSSBO.count();
}

void StarRenderer::prepareGpuBuffers() {
    GL.GenVertexArrays(1, &mScreenQuadVAOHandle);

    GL.GenBuffers(1, &mGpuDispatchHeaderSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuDispatchHeaderSSBO);
    // 16 B indirect-dispatch header (x,y,z,pad) + one tip pointer per primary tile.
    // The binning shader uses dh.tip_pointers[tileID_flat] to find each tile's
    // current write target, so the array must be sized to NUM_PRIMARY_TILES.
    size_t headerBytes = 16 + mTileCount_flat * sizeof(uint32_t);
    GL.BufferData(GL_SHADER_STORAGE_BUFFER, headerBytes, nullptr, GL_DYNAMIC_DRAW);

    GL.GenBuffers(1, &mGpuJobsSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuJobsSSBO);
    // Each Job = 16 B header (count, id_per_tile, tileID_flat, pad)
    //          + 16 B per ChunkMeta entry * GPU_MAX_JOB_CHUNKS.
    size_t const jobStrideBytes = 16 + 16 * GPU_MAX_JOB_CHUNKS;
    GL.BufferData(GL_SHADER_STORAGE_BUFFER,
        jobStrideBytes * GPU_MAX_JOBS * 10,
        nullptr, GL_DYNAMIC_COPY);

    GL.GenBuffers(1, &mGpuProfilingSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuProfilingSSBO);
    GL.BufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuProfilingStruct),
                nullptr, GL_DYNAMIC_READ);

    size_t accumElems = size_t(mScreenSize.x) * 2 * size_t(mScreenSize.y) * GPU_ACCUMULATION_LANES;
    GL.GenBuffers(1, &mGpuAccumSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuAccumSSBO);
    GL.BufferData(GL_SHADER_STORAGE_BUFFER, accumElems * sizeof(int32_t), nullptr, GL_DYNAMIC_COPY);    
}

void StarRenderer::run(
    glm::mat4 modelViewMatrix,
    glm::mat4 projectionMatrix,
    float luminanceMultiplicator,
    bool hdrEnabled,
    std::array<bool, 512> const& keys_just_pressed,
    std::array<bool, 512> const& keys_down) {  
    GL.Disable(GL_DEPTH_TEST);
    
    // calculate camera matrices
    glm::mat4 const& uMatMV = modelViewMatrix;
    glm::mat4 const& uMatP = projectionMatrix;

    //invert in high precision to better deal with bad conditioning of matrix
    glm::mat4 const uInvMV = glm::mat4(glm::inverse(glm::highp_f64mat4x4(modelViewMatrix)));            
    glm::mat4 const uInvP = glm::mat4(glm::inverse(glm::highp_f64mat4x4(projectionMatrix)));

    glm::mat4 const uViewProj = uMatP * uMatMV;

    constexpr float parsecToMeter = 3.08567758e16;
    glm::vec3 cameraPosParsec = glm::vec3(uInvMV * glm::vec4(0,0,0,1)) / parsecToMeter;

    static glm::mat4 const icrsCorrectionMatrix = glm::mat4(1.f);//no correction if we just use given cosmoscout data//glm::inverse(stars::R_icrs_to_gal);

    GL.BindImageTexture(0, mAccumulationTexture.handle(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
    GL.BindImageTexture(1, mFrameTexture.handle(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);

    static GpuStopwatch sw{};

    //TODO: use glProgramUniform instead, cleaner, more modern

    //// PASS: clear screen ////
    {
        sw.startTiming("clear");
        GL.UseProgram(mClearProgram);
        GL.Uniform2i(GL.GetUniformLocation(mClearProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
        GL.DispatchCompute((mScreenSize.x + 7) / 8, (mScreenSize.y + 7) / 8, 1);
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        sw.endTiming("clear");
    }

    //// SSBO BINDING ////
    {
        sw.startTiming("bindings");
        mGpuBatchRowsFlatSSBO.bind(0);
        mGpuBatchRowsHighPrecisionFlat.bind(6);
        mGpuChunkMetaSSBO.bind(1);
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mGpuJobsSSBO); //generated on-gpu (gpu-driven rendering)
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mGpuDispatchHeaderSSBO);
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, mGpuAccumSSBO);
        sw.endTiming("bindings");
    }

    //// PASS: clear draw list ////
    {
        sw.startTiming("drawlist_clear");
        GL.UseProgram(mDrawListClearProgram);
        // One thread per primary tile to seed count/tileID + tip_pointers[tile] = tile.
        GL.DispatchCompute(GLuint((mTileCount_flat + 63) / 64), 1, 1);
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        sw.endTiming("drawlist_clear");
    }

    static double t_start = util::time_now();

    auto previous_drawlist_config = m_config.drawlist;

    if(keys_just_pressed[GLFW_KEY_1]) m_config.drawlist.HIGH_LOD_PX_THRESHOLD *= 2;
    if(keys_just_pressed[GLFW_KEY_2]) m_config.drawlist.HIGH_LOD_PX_THRESHOLD /= 2;
    if(keys_just_pressed[GLFW_KEY_G]) m_config.drawlist.DEBUG_GRID_OVERLAY ^= true;
    if(keys_just_pressed[GLFW_KEY_M]) m_config.drawlist.DEBUG_CULLING_MINIMAP ^= true;
    if(keys_just_pressed[GLFW_KEY_L]) m_config.drawlist.DEBUG_VISUALIZE_LOD_RADIUS ^= true;
    if(keys_just_pressed[GLFW_KEY_B]) m_config.drawlist.DEBUG_VISUALIZE_BINNING ^= true;


    if(m_config.drawlist != previous_drawlist_config) {
        std::cout << "recompiling draw list program" << std::endl;
        recompileDrawListProgram();
    }

    //// PASS: generate draw list ////
    {
        sw.startTiming("drawlist");
        GL.UseProgram(mDrawListProgram);
        GL.Uniform1f(GL.GetUniformLocation(mDrawListProgram, "uTime"), static_cast<float>(util::time_now() - t_start));
        GL.Uniform1ui(GL.GetUniformLocation(mDrawListProgram, "uChunkCount"), uChunkCount);
        GL.Uniform1f(GL.GetUniformLocation(mDrawListProgram, "uChunkSize"), preprocessing::CHUNK_SIZE_PARSECS);
        GL.Uniform2i(GL.GetUniformLocation(mDrawListProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
        
        static uint32_t max = 4;
        //if(keys_just_pressed[GLFW_KEY_1]) max += 1;
        //if(keys_just_pressed[GLFW_KEY_2]) max -= 1;
        if(max < 1) max = 1;

        uint32_t m = static_cast<uint32_t>(max);

        static size_t mask = 1 | 4 | 16;
        if(keys_just_pressed[GLFW_KEY_3]) mask ^= 0x1;
        if(keys_just_pressed[GLFW_KEY_4]) mask ^= 0x4;
        if(keys_just_pressed[GLFW_KEY_5]) mask ^= 16; // == 0x10

        GL.Uniform1ui(GL.GetUniformLocation(mDrawListProgram, "uShowMask"), mask);
        GL.Uniform1ui(GL.GetUniformLocation(mDrawListProgram, "uMAX_JOB_WORK"), m);
        GL.UniformMatrix4fv(GL.GetUniformLocation(mDrawListProgram, "uMatModel"), 1, GL_FALSE, &icrsCorrectionMatrix[0][0]);
        GL.UniformMatrix4fv(GL.GetUniformLocation(mDrawListProgram, "uViewProj"), 1, GL_FALSE, &uViewProj[0][0]);
        GL.UniformMatrix4fv(GL.GetUniformLocation(mDrawListProgram, "uInvP"), 1, GL_FALSE, &uInvP[0][0]);
        GL.DispatchCompute((uChunkCount + 63) / 64, 1, 1); //for each screentile
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
        sw.endTiming("drawlist");
    }

    { // prepare profiling ssbo
        GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuProfilingSSBO);
        const uint32_t zero = 0;
        GL.ClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI,
                        GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mGpuProfilingSSBO);
    }

    //// PASS: workload-per-tile overlay (toggle with B) ////
    // Reads dh.tip_pointers + joblist tip jobs, writes a per-tile heat bar
    // into mFrameTexture. Sits between drawlist (which sets up the chain) and
    // raster (which is read-only on the chain). The drawlist barrier above
    // already publishes the SSBO writes; we add an image-access barrier after
    // so raster's own debug-overlay writes see a settled framebuffer.
    static bool workloadOverlayEnabled = false; //REVEAL(tilebased workload overlay, drawlist, workload balancing)
    if (keys_just_pressed[GLFW_KEY_B]) workloadOverlayEnabled = !workloadOverlayEnabled;
    if (workloadOverlayEnabled) {
        sw.startTiming("debugworkload");
        GL.UseProgram(mDebugVisualsProgram);
        GL.Uniform2i(GL.GetUniformLocation(mDebugVisualsProgram, "uResolution"),
                     mScreenSize.x, mScreenSize.y);
        // Chunk count that saturates the heatmap. 4 chains deep is a useful
        // default: green = primary fits, yellow = some overflow, red = hot.
        GL.Uniform1ui(GL.GetUniformLocation(mDebugVisualsProgram, "uWorkloadFullChunks"),
                      GLuint(4 * GPU_MAX_JOB_CHUNKS));
        GL.DispatchCompute(GLuint((mTileCount_flat + 63) / 64), 1, 1);
        GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        sw.endTiming("debugworkload");
    }

    auto previous_raster_config = m_config.raster;
    if(keys_just_pressed[GLFW_KEY_T]) m_config.raster.RASTERIZE_USE_TILE_ACCUMULATION ^= true;
    if(keys_just_pressed[GLFW_KEY_P]) m_config.raster.WRITE_PROFILING_COUNTERS ^= true;
    if(keys_just_pressed[GLFW_KEY_U]) m_config.raster.ENABLE_LUMI_THRESHOLD ^= true;
    if(m_config.raster != previous_raster_config) recompileRasterProgram();
    
    static float debugLumiFactor = 1.0;
    if(keys_just_pressed[GLFW_KEY_PERIOD]) debugLumiFactor *= 5.0;
    if(keys_just_pressed[GLFW_KEY_COMMA]) debugLumiFactor /= 5.0;

    //// PASS: main rasterization ////
    {
        sw.startTiming("raster");
        GL.UseProgram(mRasterProgram);
        static bool lumiThresholdEnabled = false;
        if(keys_just_pressed[GLFW_KEY_L]) lumiThresholdEnabled = !lumiThresholdEnabled;
        GL.Uniform1ui(GL.GetUniformLocation(mRasterProgram, "uLumiThresholdEnabled"), lumiThresholdEnabled);
        GL.Uniform3f(GL.GetUniformLocation(mRasterProgram, "uObserverPosWorld"), cameraPosParsec.x, cameraPosParsec.y, cameraPosParsec.z);
        GL.Uniform2i(GL.GetUniformLocation(mRasterProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
        GL.Uniform1f(GL.GetUniformLocation(mRasterProgram, "uChunkSize"), preprocessing::CHUNK_SIZE_PARSECS);
        GL.UniformMatrix4fv(GL.GetUniformLocation(mRasterProgram, "uViewProj"), 1, GL_FALSE, &uViewProj[0][0]);
        GL.UniformMatrix4fv(GL.GetUniformLocation(mRasterProgram, "uInvP"), 1, GL_FALSE, &uInvP[0][0]);
        GL.UniformMatrix4fv(GL.GetUniformLocation(mRasterProgram, "uMatModel"), 1, GL_FALSE, &icrsCorrectionMatrix[0][0]);
        GL.Uniform1ui(GL.GetUniformLocation(mRasterProgram, "uStartLayer"), 0);
        GL.Uniform1ui(GL.GetUniformLocation(mRasterProgram, "uLayerCount"), GPU_MAX_JOB_CHUNKS);
        std::cout << "newrenderer uniform: " << luminanceMultiplicator << std::endl;
        GL.Uniform1f(GL.GetUniformLocation(mRasterProgram, "uLuminanceMultiplicator"), luminanceMultiplicator * debugLumiFactor);

        //indirect dispatch
        GL.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, mGpuDispatchHeaderSSBO);
        GL.DispatchComputeIndirect(0);   // x/y/z read from offset 0 of the bound buffer
        GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        sw.endTiming("raster");
    }

    uint32_t gpuJobDispatchCount = 0;

    { // read back profiling ssbo from GPU
        GL.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                   sizeof(GpuProfilingStruct), &mProfilingStruct);
        GL.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            sizeof(uint32_t), &gpuJobDispatchCount);
    }

    //// PASS: debug text overlay ////

    uint32_t charBuf[32 * 8] = {};
    generateDebugString(sw.getAllMeasurements(), mProfilingStruct, cameraPosParsec, charBuf);

    static bool debugOverlayEnabled = true;
    if(keys_just_pressed[GLFW_KEY_I]) debugOverlayEnabled = !debugOverlayEnabled; 
    if(debugOverlayEnabled)
    {
        GL.UseProgram(mDebugStatsProgram);
        GL.Uniform2i(GL.GetUniformLocation(mDebugStatsProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
        GL.Uniform1ui(GL.GetUniformLocation(mDebugStatsProgram, "uLineCount"), 32);
        GL.Uniform1uiv(GL.GetUniformLocation(mDebugStatsProgram, "uLineChars"), 32 * 8, static_cast<GLuint const * const>(charBuf));
        GL.DispatchCompute(32, 1, 1); //one work group per line, one thread per char
        GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        GL.Disable(GL_DEPTH_TEST);
        GL.Clear(GL_COLOR_BUFFER_BIT);
    }

    //// PASS: final onscreen composition pass ////
    {
        GL.UseProgram(mScreenQuadProgram);
        GL.BindVertexArray(mScreenQuadVAOHandle);

        GL.ActiveTexture(GL_TEXTURE0);
        GL.BindTexture(GL_TEXTURE_2D, mAccumulationTexture.handle());
        GL.Uniform2i(GL.GetUniformLocation(mScreenQuadProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
        GL.Uniform1i(GL.GetUniformLocation(mScreenQuadProgram, "uAccumulationTex"), 0);
        GL.Uniform1f(GL.GetUniformLocation(mScreenQuadProgram, "uHdrEnabled"), hdrEnabled);
        
        GL.ActiveTexture(GL_TEXTURE1);
        GL.BindTexture(GL_TEXTURE_2D, mFrameTexture.handle());
        GL.Uniform1i(GL.GetUniformLocation(mScreenQuadProgram, "uDebugTex"), 1);

        GL.DrawArrays(GL_TRIANGLES, 0, 6);
    }

    //tidy up state so we don't leave TEXTURE0 or TEXTURE1 bound for later passes.
    GL.ActiveTexture(GL_TEXTURE1);
    GL.BindTexture(GL_TEXTURE_2D, 0);
    GL.ActiveTexture(GL_TEXTURE0);
    GL.BindTexture(GL_TEXTURE_2D, 0);
    GLenum e = GL.GetError();
    if(e != 0) std::cout << "GL err 0x" << std::hex << e << '\n';
}

StarRenderer::~StarRenderer() {
    GL.DeleteBuffers(1, &mGpuJobsSSBO);

    GL.DeleteVertexArrays(1, &mScreenQuadVAOHandle);

    GL.DeleteProgram(mClearProgram);
    GL.DeleteProgram(mDrawListClearProgram);
    GL.DeleteProgram(mDrawListProgram);
    GL.DeleteProgram(mRasterProgram);
    GL.DeleteProgram(mDebugStatsProgram);
    GL.DeleteProgram(mDebugVisualsProgram);
    GL.DeleteProgram(mScreenQuadProgram);
}

}
