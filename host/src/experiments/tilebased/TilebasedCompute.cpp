#include "TilebasedCompute.hpp"

#include <random>
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

struct TemperatureColor {
    uint8_t l_warm; //maybe encode in exponential for better HDR depth?
    uint8_t l_cold; //maybe encode in exponential for better HDR depth?
};

struct Star { //32 bit lowres representation
private:
    uint32_t mortonEncodeLocalPosition(glm::vec3 const& localPosition) {
        return util::encode_morton(localPosition);
    }
public:
    // encode position as 32 bit morton code, not delta yet
    uint32_t pos;
    TemperatureColor tc;

    Star(glm::vec3 const& localPosition
    ) :
    tc{0x7f, 0x7f}, //hardcoded cold/warm luminance for now
    pos {mortonEncodeLocalPosition(localPosition * 64.f)}
    {}
};

struct DeltaStar {
    uint8_t const pos_delta;
    TemperatureColor const tc;

    DeltaStar(Star const& previous, Star const& current):
    tc{current.tc},
    pos_delta{
        static_cast<uint8_t>(current.pos - previous.pos)
    }
    {
        //just for testing, remove this later or add dynamic delta encoding (huffmann or smth)
        //or just start a new StarBatch once delta is too lange (may need more tight encoding in StarBatch metainfo)
        constexpr uint64_t limit = UINT8_MAX;
        uint64_t true_delta = static_cast<uint64_t>(current.pos) - static_cast<uint64_t>(previous.pos);
        assert(true_delta < limit );
    }
};

struct Chunk {
    uint32_t globalPosEncoded; //packed world position
    glm::ivec3 globalPos; //non-packed world position, for easier access
    std::vector<Star> stars;

    uint32_t firstStarMorton = 0; //chunk-local base morton code offset for this batch of deltas
    uint32_t firstStarPtr = 0; //count of delta-stars
    uint32_t merged_count = 0;
    uint32_t original_count = 0;

    /*void updateDeltaStars(std::vector<Star> const& stars) {
        if(stars.empty()) return;
        Star const& firstStar = stars.front();

        firstStarMorton = firstStar.pos;
        original_count = stars.size();

        //first star has 0 delta
        deltaStars.emplace_back(firstStar, firstStar);
        if(stars.size() < 2) return;

        for(size_t i = 1; i < stars.size(); i++) {
            Star const& previous = stars[i - 1];
            Star const& current = stars[i];
            
            DeltaStar deltaStar(previous, current);
            if(deltaStar.pos_delta == 0) continue; //TODO: do proper temp-luminance merge in this case

            deltaStars.emplace_back(previous, current);
            merged_count++;
        }
    }*/
};

struct RawChunk {
    uint32_t globalPosEncoded; //packed world position
    glm::ivec3 globalPos; //non-packed world position, for easier access
    std::vector<RawStar> rawStarsLocal; //raw stars, in local relative space

    RawChunk() = default;
};


uint32_t packIvec8(glm::ivec3 pos) {
    constexpr int32_t offset = 0x7f;
    if(std::abs(pos.x) > offset || std::abs(pos.y) > offset || std::abs(pos.z) > offset) {
        std::cout << "assertion failed" << std::endl;
        exit(-1);
    }

    glm::uvec3 shifted = pos + glm::ivec3(offset);

    uint32_t x = (pos.x + offset) & 0xff;
    uint32_t y = (pos.y + offset) & 0xff;
    uint32_t z = (pos.z + offset) & 0xff;

    return (x << 16) | (y << 8) | z;
}

glm::ivec3 unpackIvec8(uint32_t v) {
    constexpr int32_t offset = 0x7f;

    uint8_t x = (v >> 16) & 0xff;
    uint8_t y = (v >> 8) & 0xff;
    uint8_t z = (v >> 0) & 0xff;

    glm::ivec3 pos{x,y,z};
    glm::ivec3 unshifted = pos - glm::ivec3(offset);

    return unshifted;
}

/*double pixel_world_space_width(double dist) {
    return 2.0 * dist / (960.0); //tan(45°) = 1
}*/ //TODO: use this to generate dynamic LOD levels, for now we assume fixed precision

constexpr double CHUNK_SIZE_PARSECS = 64.f;//312.5; // 10k / 64;

void bin_raw_stars(std::vector<RawStar> const& raw_stars, std::unordered_map<uint32_t, RawChunk>& rawChunksOut) {
    std::cout << "global position of first raw star: " << glm::to_string(raw_stars.front().mPosition) << std::endl;
    std::cout << "number of input raw stars:" << raw_stars.size() << std::endl;
    for(RawStar const& raw_star : raw_stars) {
        glm::vec3 const globalChunkSpacePosition = raw_star.mPosition / CHUNK_SIZE_PARSECS;
        glm::ivec3 const chunkCoord{globalChunkSpacePosition};

        uint32_t const chunkCoordEncoded = packIvec8(chunkCoord);

        if(rawChunksOut.find(chunkCoordEncoded) == rawChunksOut.end()) {
            rawChunksOut[chunkCoordEncoded].globalPosEncoded = chunkCoordEncoded;
            rawChunksOut[chunkCoordEncoded].globalPos = globalChunkSpacePosition;
        }
        
        RawStar raw_star_local = raw_star;
        raw_star_local.mPosition = glm::fract(globalChunkSpacePosition);

        rawChunksOut[chunkCoordEncoded].rawStarsLocal.push_back(raw_star_local);
    }
}

//TODO: switch to glm double (highp_f64vec3 for initial star position math)
void transformRawChunks(std::unordered_map<uint32_t, RawChunk> const& rawChunks,
                        std::unordered_map<uint32_t, Chunk>& out) {
    for (auto const& [id, rawChunk] : rawChunks) {
        out[id].globalPos = rawChunk.globalPos;
        out[id].globalPosEncoded = rawChunk.globalPosEncoded;

        // sort stars by Morton code first - critical for delta encoding
        std::vector<Star>& starsOfChunk = out[id].stars; //avoid 1 full copy
        starsOfChunk.reserve(rawChunk.rawStarsLocal.size());
        for (RawStar const& rs : rawChunk.rawStarsLocal) {
            starsOfChunk.push_back(Star(rs.mPosition));
        }
        std::sort(starsOfChunk.begin(), starsOfChunk.end(),
                  [](Star const& a, Star const& b) { return a.pos < b.pos; });
    }
}

namespace {
    float rand_float(
    float const min = 0.f,
    float const max = 1.f)
{
    static thread_local std::mt19937 gen(42);
    static thread_local std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(gen) * (max - min) + min;
}

#pragma pack(push, 1)

struct GpuChunkMeta {
    uint32_t startBatchNum = 0; //where in batch memory does the first batch start; (1 step = 256 x 256 x 4 bytes)
    uint32_t batchCount = 0; //how many batches in this chunk
    uint32_t lastBatchRowCount = 0; //leftover point rows in last batch
    uint32_t chunkID = 0; //unique location bound id, multiple batches can be assigned to one id
    //if we pack batchCount or lastRowCount, maybe we could use the leftover space to encode some LOD helper info or smth
};

constexpr size_t GPU_BATCH_LENGTH = 127; //how many rows per batch, one less than round because one reserved for baseOffsets
//maybe 256 is fine here too, have to see

constexpr size_t GPU_THREAD_COUNT = 512; //how many parallel columns per batch
constexpr size_t GPU_BATCH_SIZE = GPU_BATCH_LENGTH * GPU_THREAD_COUNT;

//batch of stars, they are uploaded sequentially into a raw uint32_t buffer on the gpu
//memory addresses can be calculated by constant known batch size and gpu chunk metadata
struct GpuBatch {
    //first row: base morton offsets, to know where to put first star of 256-column (since after the first we just get deltas)
    uint32_t baseOffsets[GPU_THREAD_COUNT] = {0};
    //following rows: morton-delta encoded 256-coalesced stars
    uint32_t data[GPU_BATCH_SIZE] = {0}; //TODO: do not use fixed batch size, instead split chunk into equal batches if it gets too heavy
    //TODO: or maybe fixed size batches are fine too, have to see.
};

//IDEA: overloaded function "gpu-serialize" that defines how to lay out data vector before passing to SSBO upload 

#pragma pack(pop)

size_t delta_0_ctr = 0;
size_t delta_ctr = 0;

void gpuPrepareChunk(Chunk const& inChunk,
    std::vector<GpuBatch>& batchesOut, //append n
    std::vector<GpuChunkMeta>& chunkMetaOut //append 1
) {
    GpuChunkMeta& meta = chunkMetaOut.emplace_back();
    meta.chunkID = inChunk.globalPosEncoded;
    meta.startBatchNum = batchesOut.size(); //assume that we append at least one batch

    //TODO: if last row is less than 256 delta-stars, we need to encode them as delta 0 and luminance 0 to avoid random stars

    meta.batchCount = std::ceil(static_cast<double>(inChunk.stars.size()) / static_cast<double>(GPU_BATCH_SIZE));
    meta.lastBatchRowCount = inChunk.stars.size() % GPU_BATCH_SIZE;

    std::vector<Star> const& stars = inChunk.stars;
    for(size_t batchIdx = 0; batchIdx < meta.batchCount; batchIdx++) {
        GpuBatch& batch = batchesOut.emplace_back(); //allocate batch to write
        
        //firstly, write base morton offsets to batch
        for(size_t i = 0; i < GPU_THREAD_COUNT; i++) {
            batch.baseOffsets[i] = stars[i * GPU_BATCH_LENGTH].pos;
        }

        //write first row of stars with 0 delta, since we know their absolute position from base morton offsets
        /*for(size_t i = 0; i < GPU_THREAD_COUNT; i++) {
            batch.data[i] = 0;
        }*/

        size_t ctr = 0;
        for(size_t thread = 0; thread < GPU_THREAD_COUNT; thread++) {
            for(size_t i = 0; i < GPU_BATCH_LENGTH; i++) {
                uint32_t delta = 0;
                if(i > 0) {;
                    size_t idx1 = thread * GPU_BATCH_LENGTH + i;
                    size_t idx0 = thread * GPU_BATCH_LENGTH + (i - 1);
                    uint32_t pos1 = stars[idx1].pos;
                    uint32_t pos0 = stars[idx0].pos;
                    delta = static_cast<uint32_t>(pos1- pos0);
                    if(delta == 0) {
                        delta_0_ctr++;
                    }
                    delta_ctr++;
                }
                size_t out_pos = thread + i * GPU_THREAD_COUNT;
                batch.data[out_pos] = delta;
                //std::cout << (int)delta << "\t";
            }
            //std::cout << std::endl;
        }
        /*for(size_t i = 0; i < 2000; i++) {
            std::cout << batch.data[i] << " ";
        }
        std::cout << std::endl;
        std::quick_exit(0);*/
        //then, append the delta stars, skip first row as first row encodes base morton offsets
        /*for(size_t row = 0; row < GPU_BATCH_LENGTH - 1; row++) {
            for(size_t i = 0; i < GPU_THREAD_COUNT; i++) {
                //for now, only write delta position
                //TODO: encode temperature and luminance
                batch.data[ (row + 1) * GPU_THREAD_COUNT + i] = 
                    stars[  (row + 1) * GPU_THREAD_COUNT + i].pos -
                    stars[   row      * GPU_THREAD_COUNT + i].pos;
            }
        }*/
    }

    //TODO: write leftover stars into last batch
    //for now just leave out leftover stars
}

}

void generateTestChunks(std::unordered_map<uint32_t, Chunk>& chunks) {
    for(size_t c = 0; c < 512; c++) {
        size_t x = c % 30;
        size_t y = c / 30;
        glm::ivec3 chunkCoord{x,y,0};
        uint32_t id = packIvec8(chunkCoord);

        chunks[id].globalPos = chunkCoord;
        chunks[id].globalPosEncoded = id;
        
        std::vector<Star>& starsOfChunk = chunks[id].stars;
        for(size_t i = 0; i < GPU_BATCH_SIZE; i++) { //see GpuChunk for why we only go to 510
            constexpr double min = 0.0;
            constexpr double max = 1.0;

            double x = rand_float(0.f, 1.f);

            /*glm::vec3 pos = glm::vec3(
                cos(i * 0.1f) / 2.f + .5f,
                sin(i * 0.1f) / 2.f + .5f,
                0.f
            );*/

            glm::vec3 pos = glm::vec3(
                rand_float(min, max),
                rand_float(min, max),
                rand_float(min, max)
            );

            starsOfChunk.emplace_back(
                pos
            );
        }

        //sort here, but will be sorted in transformRawChunks in proper impl anyways
        std::sort(
            starsOfChunk.begin(),
            starsOfChunk.end(),
            [](Star const& a, Star const& b) { return a.pos < b.pos; }
        );
    }
}

TilebasedCompute::TilebasedCompute(CameraController const& camera, Window& window)
: IComputeProgram(camera, window),
mFrameTexture{createTexture(window.getSize())}
{
    mClearProgram = createComputeProgramFromFile("src/shaders/compute_clear.comp", {});
    mRasterProgram = createComputeProgramFromFile("src/experiments/tilebased/tilebased_compute.comp",
    {
        {"THREAD_COUNT", GPU_THREAD_COUNT},
        {"BATCH_LENGTH", GPU_BATCH_LENGTH}
    }
    );

    /*std::unique_ptr<Stars> stars = get_stars();
    std::vector<Star> star_vector_source = stars->getStars();*/

    //std::unordered_map<uint32_t, RawChunk> rawChunks;
    //bin_raw_stars(rawStars, rawChunks);
    
    std::unordered_map<uint32_t, Chunk> chunks;
    //transformRawChunks(rawChunks, chunks);
    generateTestChunks(chunks);

    //std::vector<uint32_t> gpuStars;
    std::vector<GpuChunkMeta> gpuChunkMetas;
    gpuChunkMetas.reserve(chunks.size());

    std::vector<GpuBatch> gpuBatches;
    //copy delta stars from chunks sequentially, pass global offset ptr to chunk
    for(auto& [id, chunk] : chunks) {
        gpuPrepareChunk(chunk, gpuBatches, gpuChunkMetas);
    }

    std::vector<uint32_t> gpuBatchesFlat;
    gpuBatchesFlat.reserve(gpuBatches.size());
    for(auto const& gpuBatch : gpuBatches) {
        //serialize base offsets
        for(size_t i = 0; i < GPU_THREAD_COUNT; i++) {
            gpuBatchesFlat.push_back(gpuBatch.baseOffsets[i]);
        }

        //serialize main batch table
        for(size_t i = 0; i < GPU_BATCH_SIZE; i++) {
            gpuBatchesFlat.push_back(gpuBatch.data[i]);
        }
    }

    std::cout << "delta_0_ctr:\t" << delta_0_ctr / 1'000'000 << std::endl;
    std::cout << "delta_ctr:\t" << delta_ctr / 1'000'000 << std::endl;
    std::cout << "merged ct.:\t" << (delta_ctr - delta_0_ctr) / 1'000'000 << std::endl;
    std::cout << "remain ratio:\t" << static_cast<double>(delta_ctr - delta_0_ctr) / static_cast<double>(delta_ctr) << std::endl;
    //std::quick_exit(0);

    //upload data to GPU
    mGpuChunkMetaSSBO.create(gpuChunkMetas);
    mGpuBatchesFlatSSBO.create(gpuBatchesFlat);
}

void TilebasedCompute::updateKeys() {

};

void TilebasedCompute::setUniforms() {
 
}

void TilebasedCompute::run() {
    glBindImageTexture(0, mFrameTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

    //TODO: use glProgramUniform instead, so that we dont have to pass the matrices trough class members
    //clear compute screen buffer
    glUseProgram(mClearProgram);
    glUniform2i(glGetUniformLocation(mClearProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
    glDispatchCompute((mScreenSize.x + 7) / 8, (mScreenSize.y + 7) / 8, 1);

    //run main rasterization
    glUseProgram(mRasterProgram);

    mGpuChunkMetaSSBO.bind(1);
    mGpuBatchesFlatSSBO.bind(2);

    glUniform2i(glGetUniformLocation(mRasterProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
    //glUniform1ui(glGetUniformLocation(mRasterProgram, "uChunkCount"), mChunkSSBO.count());
    glDispatchCompute(30, 17, 1);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

GLuint TilebasedCompute::getFrameTexture() {
    return mFrameTexture;
}

TilebasedCompute::~TilebasedCompute() {
    glDeleteTextures(1, &mFrameTexture);
    glDeleteProgram(mClearProgram);
    glDeleteProgram(mRasterProgram);
}

