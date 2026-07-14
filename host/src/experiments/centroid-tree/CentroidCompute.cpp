#include "CentroidCompute.hpp"

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

struct Node {
    glm::highp_f64vec3 center; //global centroid position
    double radius; //centroid radius
    size_t sum; //sum of all stars in all child nodes
    std::vector<RawStar> stars;
    //bool isLeaf = false;
};

constexpr double BUCKET_SIZE_PARSECS = 16.0;
constexpr double MERGE_THRESHOLD_PARSECS = 500.0;

uint64_t packIvec21(glm::ivec3 pos) {
    constexpr uint64_t l = 0x100000;
    if(std::abs(pos.x) > l || std::abs(pos.y) > l || std::abs(pos.z) > l) {
        std::cout << "assertion failed" << std::endl;
        exit(-1);
    }

    glm::uvec3 shifted = pos + glm::ivec3(l);

    uint64_t x = (pos.x + l) & 0x1fffff;
    uint64_t y = (pos.y + l) & 0x1fffff;
    uint64_t z = (pos.z + l) & 0x1fffff;

    return (x << 42) | (y << 21) | z;
}

glm::ivec3 unpackIvec21(uint64_t v) {
    constexpr uint64_t l = 0x100000;

    uint64_t x = (v >> 42) & 0x1fffff;
    uint64_t y = (v >> 21) & 0x1fffff;
    uint64_t z = (v >>  0) & 0x1fffff;

    glm::ivec3 pos{x,y,z};
    glm::ivec3 unshifted = pos - glm::ivec3(l);

    return unshifted;
}


uint64_t bucketFromStarPosition(decltype(RawStar::mPosition) const& pos) {
    glm::ivec3 discretePos = glm::ivec3(pos / BUCKET_SIZE_PARSECS);
    uint64_t encoded = packIvec21(discretePos);
    return encoded;
}

double pixel_world_space_width(double dist) {
    return 2.0 * dist / (960.0); //tan(45°) = 1
}

static uint32_t s_id = 0;
void sort_to_buckets(std::vector<RawStar>& stars, std::vector<RawStar>& near_set, std::vector<RawStar>& far_set) {
    std::unordered_map<uint64_t, std::vector<RawStar*>> buckets; //rounded to 100 parsec buckets

    std::cout << "first stars position: " << glm::to_string(stars.front().mPosition) << std::endl;
    std::cout << "number of input stars:" << stars.size() << std::endl;
    size_t discard_counter = 0;
    size_t counter = 0;
    double max_len = 0.0;
    for(RawStar& star : stars) {
        //if(counter++ > 500000) continue;
        /*auto p = glm::abs(star.mPosition);
        double highest_dist = std::max({p.x, p.y, p.z});
        if(highest_dist > max_dist) {
            max_dist = highest_dist;
            max_star = star;
        }*/

        double len = glm::length(star.mPosition);
        /*if(len > 25'000.0 || len < 1.0) {
            discard_counter++;
            continue;
        }*/
        
        if(len > max_len) max_len = len;

        /*if(len < MERGE_THRESHOLD_PARSECS) {
            near_set.push_back(star);
            continue;
        }*/

        //if far away put into buckets for easy merging
        uint64_t bucket_id = bucketFromStarPosition(star.mPosition);
        buckets[bucket_id].push_back(&star);
    }

    { // --- UPDATED HISTOGRAM CODE ---
        struct DistanceBin {
            double sumMinDist = 0.0;
            double absoluteMinDist = std::numeric_limits<double>::max();
            uint32_t count = 0;
        };
        std::map<uint32_t, DistanceBin> hist;

        std::cout << "Calculating neighbour distances... (this may take a moment)" << std::endl;

        for(auto& star : stars) {
            double starDistFromOrigin = glm::length(star.mPosition);
            uint32_t binIdx = static_cast<uint32_t>(starDistFromOrigin / 100.0);
            
            if(binIdx > 200) continue; // Cut off at 20,000 pc as per original code

            // Find the nearest neighbor in the 3x3x3 bucket neighborhood
            double nearestNeighborDist = std::numeric_limits<double>::max();
            glm::ivec3 discretePos = glm::ivec3(star.mPosition / BUCKET_SIZE_PARSECS);

            for(int x = -1; x <= 1; ++x) {
                for(int y = -1; y <= 1; ++y) {
                    for(int z = -1; z <= 1; ++z) {
                        uint64_t bucketId = packIvec21(discretePos + glm::ivec3(x, y, z));
                        auto it = buckets.find(bucketId);
                        if(it != buckets.end()) {
                            for(RawStar* other : it->second) {
                                if(other == &star) continue;
                                double d = glm::distance(star.mPosition, other->mPosition);
                                if(d < nearestNeighborDist) nearestNeighborDist = d;
                            }
                        }
                    }
                }
            }

            // If a neighbor was found, add to histogram
            if(nearestNeighborDist != std::numeric_limits<double>::max()) {
                DistanceBin& bin = hist[binIdx];
                bin.count++;
                bin.sumMinDist += nearestNeighborDist;
                if(nearestNeighborDist < bin.absoluteMinDist) {
                    bin.absoluteMinDist = nearestNeighborDist;
                }
            }
        }

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "\n> Histogram of Neighbour Distances (100pc steps from origin) <" << std::endl;
        std::cout << "Bin (pc)\tCount\tAvg Min Dist\t%Pixelt" << std::endl;
        std::cout << "------------------------------------------------------------" << std::endl;
        
        double sumPercent = 0.0;
        double sumCount = 0;
        double sumCountMerged = 0;

        for(size_t extraDist = 1; extraDist < 2000; extraDist += 50) {

        for(auto const& [binIdx, data] : hist) {
            double avgMin = data.sumMinDist / data.count;
            sumPercent += static_cast<double>(data.count) / stars.size();

            double relativeScreenSpaceArea = avgMin / pixel_world_space_width(binIdx * 100 + extraDist);
            double starMergeFactor = 1.0  * std::pow((1.0/relativeScreenSpaceArea), 3.0);
            
            sumCountMerged += data.count / std::max(starMergeFactor, 1.0);
            sumCount += data.count;

            /*std::cout << std::setprecision(4)
                      << (binIdx * 100) << "pc\t-  " 
                      << (binIdx * 100 + 100) << "\t"
                      << data.count << "\t"
                      << sumPercent << "%\t"
                      << avgMin << "pc\t"
                      << relativeScreenSpaceArea << "%\t"
                      << starMergeFactor << "\t";
                      //<< data.absoluteMinDist
                      for(size_t x = 0; x < data.count / starMergeFactor / 100; x++){
                        std::cout << "#";
                      }
                      std::cout << std::endl;*/
        }
        //std::cout << "> ---------------------------------------------------------- <" << std::endl;
        //std::cout << "actual count:  " << sumCount << std::endl;
        //std::cout << "reduced count: " << sumCountMerged << std::endl;
        std::cout << "extraDist: " << extraDist << "\trelative percent to be rendered: " << sumCountMerged / sumCount * 1000.0 << " million" << std::endl;
        }
    }


    { //histogram code
        std::map<size_t, uint32_t> countHistrogram;
        //for(auto it = buckets.begin(); it != buckets.end(); it++) {
        //    size_t count = it->second.size();
        for(auto const& s : stars) {
            uint32_t dist = glm::length(s.mPosition) / 100.0;
            uint32_t count = dist;
            if(dist > 200) continue;
            if(countHistrogram.find(count) == countHistrogram.end()){
                countHistrogram[count] = 1;
            }
            else {
                countHistrogram[count]++;
            }
        }
        
        size_t i = 0;
        //std::cout << "> histogramm of stars per bucket <" << std::endl;
        double sumPercent = 0.0;
        std::cout <<   "> histogram of stars per distance (100 parsec distance steps, cut off at 200 * 100 pc)<" << std::endl;
        for(auto it = countHistrogram.begin(); it != countHistrogram.end(); it++) {
            double percent = static_cast<double>(it->second) / stars.size() * 100.0;
            sumPercent += percent;
            std::cout << it->first << "\t" << percent << "%" << std::endl;
            //if(i++ > 50000) break;
        }
        std::cout << "> ------------------------------ <" << std::endl;
        std::cout << "percent of stars that fall into 200*100pc range from origin" << std::endl;
        std::cout << sumPercent << "%" << std::endl;
    }

    //exit(0);

    std::cout << "discarded:        " << std::setw(10) << std::right << discard_counter << std::endl;
    std::cout << "max_len: " << max_len << std::endl;
    std::cout << "number of buckets:" << buckets.size() << std::endl;

    std::unordered_set<uint32_t> visited_star_ids{};
    size_t runs = 0;

    // Convert each bucket of stars into a single Node (Centroid)
    far_set.reserve(buckets.size() / 2); //just a heuristic guess
    for (auto it = buckets.begin(); it != buckets.end(); it++) {
        if(it->second.empty()) continue;

        uint64_t centerEncoded = it->first;
        glm::ivec3 center = unpackIvec21(centerEncoded);

        for(size_t i = 0; i < it->second.size(); i++) {
            RawStar const * const reference_star = it->second[i]; //pick one star to try merging into
            if(visited_star_ids.count(reference_star->id)) continue;
            //std::cout << glm::to_string(reference_star->mPosition) << std::endl;
            //std::cout << reference_star->id << std::endl;
            if(runs++ % 10000 == 0) {
                std::cout << runs << std::endl;
            }

            size_t starCount{0};
            glm::highp_f64vec3 posSum{0.0};
            double magnitudeSum{0.0};

            std::vector<RawStar*> targetsToWrite{};

            for(int x = -1; x <= 1; x++) {
                for(int y = -1; y <= 1; y++) {
                    for(int z = -1; z <= 1; z++) {
                        glm::ivec3 offset = glm::ivec3{x,y,z};
                        glm::ivec3 coord = center + offset;
                        uint64_t coordEncoded = packIvec21(coord);

                        auto localIt = buckets.find(coordEncoded);
                        if(localIt == buckets.end()) continue;
                        
                        for(auto& s : localIt->second) {
                            if(visited_star_ids.find(s->id) != visited_star_ids.end()) continue;
                            double distance = glm::distance(s->mPosition, reference_star->mPosition);
                            if(distance > 20.0) continue;
                            //std::cout << distance << std::endl;

                            posSum += s->mPosition;
                            magnitudeSum += s->mMagnitude;
                            starCount++;
                            targetsToWrite.push_back(s);
                            visited_star_ids.emplace(s->id);
                        }
                    }
                }
            }

            if(starCount == 0) continue;


            double magnitudeAvg = magnitudeSum / static_cast<double>(starCount);
            glm::highp_f64vec3 posAvg = posSum * (1.0 / static_cast<double>(starCount));

            RawStar mergedStar;
            mergedStar.mTEff = 0;
            mergedStar.mMagnitude = magnitudeAvg;
            mergedStar.mPosition = reference_star->mPosition;

            mergedStar.id = s_id;
            s_id++;

            for(RawStar* target : targetsToWrite) {
                target->target_id = mergedStar.id;
            }

            far_set.push_back(mergedStar);
        }

        // Calculate Bounding Radius
        // This is critical: it tells the GPU when to "split" the node back into stars
        /*double maxDistSq = 0.0;
        for (auto const& s : starList) {
            double d2 = glm::distance(s.mPosition, node.center);
            if (d2 > maxDistSq) maxDistSq = d2;
        }
        node.radius = std::sqrt(maxDistSq);

        // Store the original stars in the node for high-detail zooming
        node.stars = std::move(starList);*/

        //farSet.push_back(node);
    }
    //exit(0);
}

void augment_stars(std::vector<RawStar>& stars) {
    size_t const original_size = stars.size();
    for(size_t i = 0; i < original_size; i++) {
        RawStar const& star = stars[i];
        stars.emplace_back(star);
        glm::vec3 offset = glm::highp_f64vec3(util::rand_u32(), util::rand_u32(), util::rand_u32()) * (1.0 / static_cast<double>(UINT32_MAX)) * 50.0;
        stars.back().mPosition += offset;
    }
}

void offset_stars(std::vector<RawStar>& stars) {
    constexpr glm::highp_f64vec3 offset{5000.0};
    for(auto& s : stars) {
        s.mPosition += offset;
    }
}

CentroidCompute::CentroidCompute(CameraController const& camera, Window& window, size_t const CHUNK_SIZE, size_t const WARP_SIZE)
: IComputeProgram(camera, window),
mWarpSize{WARP_SIZE}
{
    //mClearProgram = createComputeProgramFromFile("src/shaders/compute_clear.comp", 64);
    //mRasterProgram = createComputeProgramFromFile("src/experiments/centroid-tree/compute_rasterize_individual.comp", WARP_SIZE);

    std::unique_ptr<Stars> stars = get_stars();
    std::vector<RawStar> star_vector_source = stars->getStars();

    auto refDir = glm::highp_f64vec3(0.0);
    size_t count = 0;
    for(size_t i = 0; i < star_vector_source.size(); i += 317) {
        refDir += star_vector_source[i].mPosition;
        count++;
    }
    refDir /= static_cast<double>(count);
    refDir = glm::normalize(refDir);

    auto refPos = glm::vec3(0.0);//refDir * 20000.0;

    //generate a few test stars
    std::vector<RawStar> star_vector;
    for(size_t i = 0; i < star_vector_source.size(); i++) {
        double dist = glm::length(star_vector_source[i].mPosition);
        if(!(10 < dist && dist < 5000000)) continue;
        star_vector.push_back(star_vector_source[i]);
        star_vector.back().mPosition -= refPos;
    }
    /*for(size_t i = 0; i < 1000; i++) {
        RawStar star;
        star.mPosition = glm::highp_f64vec3(
            util::rand_u32(),
            util::rand_u32(),
            util::rand_u32()
        ) * (1.0 / static_cast<double>(UINT32_MAX)) * 100.0;
        star.id = i;
        star_vector.push_back(star);
    }*/

    //offset_stars(star_vector);

    /*for(size_t i = 0; i < 1; i++) { //multiply by 8 (40mil stars)
        augment_stars(star_vector);
    }*/

    std::vector<RawStar> near_set;
    std::vector<RawStar> far_set;

    sort_to_buckets(star_vector, near_set, far_set); //modifies stars by writing id of merged target star
    std::cout << "star_vector size: " << std::setw(10) << std::right << star_vector.size() << std::endl;
    std::cout << "near_set size:    " << std::setw(10) << std::right << near_set.size() << std::endl;
    std::cout << "far_set size:     " << std::setw(10) << std::right << far_set.size() << std::endl;

    std::vector<glm::uvec4> orig_data;
    for(auto s: star_vector) {
        s.mPosition = (s.mPosition + 100000.0) * 1000.0;
        orig_data.push_back(
            glm::uvec4(
                s.mPosition.x,
                s.mPosition.y,
                s.mPosition.z,
                s.target_id
            )
        );
    }
    
    //exit(0);

    std::vector<glm::uvec4> near_data;
    std::vector<glm::uvec4> far_data;
    for(auto s: near_set) {
        s.mPosition = (s.mPosition + 100000.0) * 1000.0;
        near_data.push_back(
            glm::uvec4(
                s.mPosition.x,
                s.mPosition.y,
                s.mPosition.z,
                0
            )
        );
    }

    for(auto s: far_set) {
        s.mPosition = (s.mPosition + 100000.0) * 1000.0;
        far_data.push_back(
            glm::uvec4(
                s.mPosition.x,
                s.mPosition.y,
                s.mPosition.z,
                0
            )
        );
    }

    mOriginalSSBO.create(orig_data);
    //upload data to GPU
    mNearSSBO.create(near_data);
    mFarSSBO.create(far_data);
}

static bool showNear = false;
static bool showFar = true;
static bool showOriginal = true;

void CentroidCompute::updateKeys() {
    if(mWindow.wasKeyJustPressed(GLFW_KEY_1)) showNear = !showNear;
    if(mWindow.wasKeyJustPressed(GLFW_KEY_2)) showFar = !showFar;
    if(mWindow.wasKeyJustPressed(GLFW_KEY_3)) showOriginal = !showOriginal;

};

void CentroidCompute::setUniforms() {
 
}

void CentroidCompute::run() {
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


    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uMatMV"), 1, GL_FALSE, &uMatMV[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uMatP"), 1, GL_FALSE, &uMatP[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uInvMV"), 1, GL_FALSE, &uInvMV[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uInvP"), 1, GL_FALSE, &uInvP[0][0]);

    glUniformMatrix4fv(glGetUniformLocation(mRasterProgram, "uViewProj"), 1, GL_FALSE, &uViewProj[0][0]);

    glUniform2i(glGetUniformLocation(mRasterProgram, "uResolution"), mScreenSize.x, mScreenSize.y);
    glUniform3f(glGetUniformLocation(mRasterProgram, "uCameraPos"),
    mCamera.cameraPos.x, mCamera.cameraPos.y, mCamera.cameraPos.z);

    glUniform1i(glGetUniformLocation(mRasterProgram, "uShowParent"), false);

    if(showFar) {   
        mFarSSBO.bind(1);
        glUniform3f(glGetUniformLocation(mRasterProgram, "uColor"), 1,0,0);
        glDispatchCompute(mFarSSBO.count(), 1, 1);
        mFarSSBO.unbind();
    }

    /*if(showNear) {
        mNearSSBO.bind(1);
        glUniform3f(glGetUniformLocation(mRasterProgram, "uColor"), 1,1,0);
        glDispatchCompute(mNearSSBO.count(), 1, 1);
        mNearSSBO.unbind();
    }*/

    if(showOriginal) {
        mOriginalSSBO.bind(1);
        mFarSSBO.bind(2);
        glUniform1i(glGetUniformLocation(mRasterProgram, "uShowParent"), true);
        glUniform3f(glGetUniformLocation(mRasterProgram, "uColor"), 1,1,0);
        glDispatchCompute(mOriginalSSBO.count(), 1, 1);
        glUniform1i(glGetUniformLocation(mRasterProgram, "uShowParent"), false);
        mOriginalSSBO.unbind();
    }

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

CentroidCompute::~CentroidCompute() {
    glDeleteProgram(mClearProgram);
    glDeleteProgram(mRasterProgram);
}


/*

    glm::ivec3 startPos = glm::ivec3(0,0,0);
    uint64_t startPosEncoded = bucketFromStarPosition(startPos);
    std::cout << std::hex << startPosEncoded << std::endl;
    for(size_t i = 0; i < 10; i++) {
        glm::ivec3 pos = glm::ivec3(i,0,0);
        uint64_t encoded = bucketFromStarPosition(pos);
        std::cout << "searching at " << encoded << "\t" << glm::to_string(pos) << std::endl;
        if(buckets.count(encoded) == 0) continue;
        std::cout << "found " << std::dec << buckets[encoded].stars.size() << " stars here" << std::endl;
    }

    constexpr size_t MAX_STARS_PER_NODE = 1000;

    decltype(RawStar::mPosition) pos{0,0,0};
    //for(size_t i = 0; i < MAX_STARS_PER_NODE; i++) {
        size_t searchPosEncoded = bucketFromStarPosition(pos);
        auto const& searchList = buckets.at(searchPosEncoded);

        std::vector<RawStar> sorted = searchList;
        std::sort(sorted.begin(), sorted.end(), //can this be optimized if we know we only need top 100 elements out of e.g. 1000?
        //TODO: improve search speed by presorting in z-curve order
        [pos](RawStar const& a, RawStar const& b) {
            double distA = glm::distance(pos, a.mPosition);
            double distB = glm::distance(pos, b.mPosition);
            return distA > distB;
        });

        Node node;
        node.center = pos;
        for(auto& star : sorted) {
            if(!star.processed) {
                star.processed = true;
                node.stars.push_back(star);
            }
        }

        std::nth_element(sorted.begin(), sorted.begin() + MAX_STARS_PER_NODE, sorted.end(), 
        [pos](RawStar& a, RawStar& b) {
            if(a.processed != b.processed) {

                return !a.processed;
            }
            return glm::distance(pos, a.mPosition) > glm::distance(pos, b.mPosition); 
        });

        //std::copy_n(node.stars.begin(), MAX_STARS_PER_NODE, .)

    //}

*/