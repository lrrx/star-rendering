#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <array>
#include <sstream>

#include <glm/glm.hpp>

#include "window.hpp"
#include "util/util.hpp"

#include "IComputeProgram.hpp"

#include "experiments/centroid-tree/CentroidCompute.hpp"
#include "stars/stars.hpp"
#include "filesystem"

/*int main(int argc, char**argv) {
    if(argc < 2) return 0;
    std::string sourceFilename = argv[1];
    Stars stars;
    stars.loadFromGaiaCSV(sourceFilename);

    std::string targetFilename = sourceFilename + ".bin";
    stars.writeStarCache(targetFilename);

    return 0;
}*/

uint64_t packIvec(glm::ivec3 v) {
    v += UINT16_MAX / 2;

    uint64_t x = static_cast<uint16_t>(v.x);
    uint64_t y = static_cast<uint16_t>(v.y); 
    uint64_t z = static_cast<uint16_t>(v.z);

    return (x << 32) | (y << 16) | (z);
}

void countDensities(std::vector<Star> const& stars, std::string outFilename) {
    std::unordered_map<uint64_t, size_t> boxBuckets{};

    for(auto const& s : stars) {
        glm::ivec3 roundedPosition = s.mPosition / 312.5;
        uint64_t coordEncoded = packIvec(roundedPosition);

        if( std::abs(roundedPosition.x) > UINT16_MAX / 2 ||
            std::abs(roundedPosition.y) > UINT16_MAX / 2 ||
            std::abs(roundedPosition.z) > UINT16_MAX / 2    
        ) {
            coordEncoded = UINT64_MAX;
        }

        if(boxBuckets.find(coordEncoded) == boxBuckets.end()) {
            boxBuckets[coordEncoded] = 1;
        }
        else {
            boxBuckets[coordEncoded]++;
        }
    }

    std::stringstream ss;

    for(auto [coordEncoded, count] : boxBuckets) {
        ss << std::hex << coordEncoded << "\t" << std::dec << count << "\n";
    }
    ss << std::endl;

    std::ofstream outFileStream(outFilename);
    outFileStream << ss.str();
    outFileStream.close();
}

int main(int argc, char** argv) {
    if(argc < 2) return 0;
    std::string sourceBinaryFile = argv[1];
    Stars stars;
    stars.readStarCache(sourceBinaryFile);

    std::vector<Star> star_vector = stars.getStars();
    std::string outFilename = sourceBinaryFile + "_counts.txt";
    countDensities(star_vector, outFilename);
}