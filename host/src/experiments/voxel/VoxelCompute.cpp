#include "VoxelCompute.hpp"

#include <iostream>
#include <glm/glm.hpp>
#include <map>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include "../../camera/camera_controller.hpp"
#include "../../procgen/cuboid_generator.hpp"
#include "../../procgen/better_galaxy_gen.hpp"
#include "../../procgen/milky_way_generator.hpp"
#include "../../util/util.hpp"
#include "../../util/shader_utils.hpp"

constexpr size_t NUM_POINTS = 1'000'000 * 20;
constexpr int GRID_SIZE = 256;
constexpr float CELL_SIZE = 1.0f;

struct CellInfo {
    uint32_t id = 0;
    uint32_t firstIdx = 0;
    uint32_t len = 0;
    uint32_t color = 0;
};

std::ostream& operator<<(std::ostream& out, CellInfo const& cellInfo) {
    out << std::hex;
    out << cellInfo.id << "\t";
    out << std::dec;
    out << cellInfo.firstIdx << "\t";
    out << cellInfo.len << "\t";
    //out << cellInfo.color;
    return out;
}

struct Cell {
    CellInfo cellInfo;
    std::vector<uint32_t> positions;
};

void VoxelCompute::updateKeys() {
    for(size_t i = 0; i < debugTogglesKeymap.size(); i++) {
        auto const& pair = debugTogglesKeymap[i];
        if(mWindow.wasKeyJustPressed(pair.second)) keyStates[i] = !keyStates[i];
    }
}

glm::uvec3 get_cell_pos(glm::vec3 worldPos) {
    glm::ivec3 cellPos = glm::ivec3(worldPos / CELL_SIZE) + GRID_SIZE / 2;

    glm::clamp(cellPos, 0, 255);

    return glm::uvec3(cellPos);
}

uint32_t encode_cell_u32(glm::uvec3 cellPos) {
    uint32_t cellId = static_cast<uint32_t>(
    static_cast<uint8_t>(cellPos.x) << 16 |
    static_cast<uint8_t>(cellPos.y) << 8 |
    static_cast<uint8_t>(cellPos.z));
    return cellId;
}

uint32_t packLocalPosition(glm::vec4 worldPos) {
    worldPos = glm::fract(worldPos);
    worldPos *= 1.0;

    // We assume worldPos is already the relative offset (glm::modf result)
    auto packAxis = [](float v) {
        // Map [0, CELL_SIZE] -> [0, 1.0] -> [0, 1023]
        // Note: if you store relative offsets from 0 to CELL_SIZE:
        float normalized = v / 1.0f; // Replace 1.0f with CELL_SIZE if needed
        return static_cast<uint32_t>(glm::clamp(normalized * 1023.0f, 0.0f, 1023.0f));
    };

    uint32_t x = packAxis(worldPos.x);
    uint32_t y = packAxis(worldPos.y);
    uint32_t z = packAxis(worldPos.z);

    return (x << 20) | (y << 10) | z;
}

constexpr bool use_morton = false;

void generate_world(std::vector<uint32_t>& raw_points, std::vector<CellInfo>& cell_infos, std::function<glm::vec4()> point_generator, size_t const CHUNK_SIZE) {
     //allocate linearized fixed size voxel grid "buckets" (cells) for each position
    std::map<uint32_t, Cell> cells;

    //std::unique_ptr<Stars> stars = get_stars();
    //auto const& star_vec = stars->getStars();

    for (size_t i = 0; i < NUM_POINTS; i++) {
        glm::vec4 worldPos = point_generator();// - glm::vec4(20.f);
        /*if(std::abs(worldPos.x) > 127.f) continue;
        if(std::abs(worldPos.y) > 127.f) continue;
        if(std::abs(worldPos.z) > 127.f) continue;*/

        glm::uvec3 cellPos = get_cell_pos(worldPos);
        uint32_t cellId = encode_cell_u32(cellPos);

        uint32_t morton = util::encode_morton(cellPos);

        glm::uvec3 decoded_morton_cell = util::decode_morton(morton);
        //assert(decoded_morton_cell == cellPos);

        const uint32_t array_target_position = (use_morton ? morton : cellId);

        cells[array_target_position].positions.push_back(
            packLocalPosition(worldPos)
        );
        cells[array_target_position].cellInfo.id = cellId;
    }

    // Build cell index lists
    uint32_t index = 0;
    for (auto const& [morton, cell] : cells) {
        if (cell.positions.empty()) continue;

        size_t firstIdx = index;
        size_t len = cell.positions.size();

        for(auto const& point : cell.positions) {
            raw_points.push_back(point);
        }
        //raw_points.insert(raw_points.end(), cell.positions.begin(), cell.positions.end());

        for (size_t i = 0; i < len; i += CHUNK_SIZE) {
            CellInfo info;
            info.id = cell.cellInfo.id;
            info.firstIdx = static_cast<uint32_t>(firstIdx + i);
            info.len = static_cast<uint32_t>(std::min(len - i, CHUNK_SIZE));
            info.color = morton;//util::hash_u32(info.id);
            cell_infos.push_back(info);
        }

        index += len;
    }

    return;
}

VoxelCompute::VoxelCompute(CameraController const& camera, Window& window, size_t const CHUNK_SIZE, size_t const WARP_SIZE)
: IComputeProgram(camera, window),
mWarpSize{WARP_SIZE}
{
    //mClearProgram = createComputeProgramFromFile("src/shaders/compute_clear.comp", 64);
    //mRasterProgram = createComputeProgramFromFile("src/shaders/compute_rasterize.comp", WARP_SIZE);

    //generate data
    std::vector<uint32_t> points;
    std::vector<CellInfo> cell_infos;

    generate_world(points, cell_infos, random_cuboid_position, CHUNK_SIZE);

    mVisibleCellCount = static_cast<uint32_t>(cell_infos.size());
    std::cout << "Generated " << points.size() << " points" << std::endl;

    /*for(auto const& cell_info : cell_infos) {
        std::cout << cell_info << std::endl;
    }
    exit(-1);*/

    //upload data to GPU
    mPointSSBO.create(points);
    mCellSSBO.create(cell_infos);
}

void VoxelCompute::setUniforms() {
 
}

void VoxelCompute::run() {
   // calculate camera matrices
    glm::mat4 uMatMV = mCamera.get_view();
    glm::mat4 uMatP = mCamera.get_projection();

    glm::mat4 uInvMV = glm::inverse(uMatMV);
    glm::mat4 uInvP = glm::inverse(uMatP);

    glm::mat4 uViewProj = uMatP * uMatMV;
    //TODO: use glProgramUniform instead, so that we dont have to pass the matrices trough class members
    //clear compute screen buffer
    glUseProgram(mClearProgram);
    glUniform2i(glGetUniformLocation(mClearProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
    glDispatchCompute((mScreenSize.x + 7) / 8, (mScreenSize.y + 7) / 8, 1);

    //run main rasterization
    glUseProgram(mRasterProgram);

    mPointSSBO.bind(1);
    mCellSSBO.bind(2);
    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uMatMV"), 1, GL_FALSE, &uMatMV[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uMatP"), 1, GL_FALSE, &uMatP[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uInvMV"), 1, GL_FALSE, &uInvMV[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uInvP"), 1, GL_FALSE, &uInvP[0][0]);

    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uViewProj"), 1, GL_FALSE, &uViewProj[0][0]);

    glUniform2i(glGetUniformLocation(mRasterProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
    glUniform3f(glGetUniformLocation(mRasterProgram, "uCameraPos"),
    mCamera.cameraPos.x, mCamera.cameraPos.y, mCamera.cameraPos.z);

    glUniform1ui(glGetUniformLocation(mRasterProgram, "uCellCount"), mVisibleCellCount);
    glUniform1ui(glGetUniformLocation(mRasterProgram, "uGridSize"), GRID_SIZE);
    glUniform1f(glGetUniformLocation(mRasterProgram, "uCellSize"), CELL_SIZE);
    glUniform1f(glGetUniformLocation(mRasterProgram, "uGlobalScale"), 1.f);
    //glUniform1i(glGetUniformLocation(rasterProgram, "uFrameUS"), ));

    //set debug key mappings
    for(size_t i = 0; i < debugTogglesKeymap.size(); i++) {
        char const*const name = debugTogglesKeymap[i].first.c_str();
        GLint glValue = keyStates[i] ? GL_TRUE : GL_FALSE;
        glUniform1i(glGetUniformLocation(mRasterProgram, name), glValue);
    }
    //uint32_t const numGroups = (visibleCellCount + 64 - 1) / 64;
    glDispatchCompute(mVisibleCellCount, 1, 1); //1 work group processes 1 cell

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

VoxelCompute::~VoxelCompute() {
    glDeleteProgram(mClearProgram);
    glDeleteProgram(mRasterProgram);
}