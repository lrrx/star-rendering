#include "stars.hpp"

#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sys/stat.h>
#include <memory>
#include <iomanip>
#include <cstdint>
#include <glm/glm.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/string_cast.hpp>

#include "../util/util.hpp"
#include <RawStar.hpp>

// GLM_PI for consistency
#ifndef GLM_PI
#define GLM_PI 3.14159265358979323846
#endif

#define CACHE_VERSION 1

namespace {

glm::vec3 sphericalToCartesian(float ra, float dec, float parallax) {
    float distance = (parallax > 0.0f) ? (1000.0f / parallax) : 0.0f;
    
    glm::vec3 position;
    position.x = distance * std::cos(dec) * std::cos(ra);
    position.y = distance * std::cos(dec) * std::sin(ra);
    position.z = distance * std::sin(dec);
    
    return position;
}

    /**
     * Calculate effective temperature from Bp-Rp color index (Gaia formula)
     */
float calculateTEffFromColor(float bp_rp) {
    float logTeff = 3.999f - 0.654f * bp_rp + 0.709f * bp_rp * bp_rp 
                    - 0.316f * bp_rp * bp_rp * bp_rp;
    return std::pow(10.0f, logTeff);
}

/**
 * Helper: Split string by delimiter
 */
std::vector<std::string> splitString(const std::string& s, char delimiter)  {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

/**
 * Helper: Convert degrees to radians
 */
/*float degreesToRadians(float degrees) {
    return degrees * static_cast<float>(GLM_PI) / 180.0f;
}*/

/**
 * Parse a single Gaia line into a RawStar structure
 */
bool parseGaiaLine(const std::vector<std::string>& items, RawStar& star) {
    if (items.size() < 15) {
        return false;
    }

    // 2. Define required indices for validation
    // Index: 1(ID), 2(RA), 4(Dec), 6(Plx), 10(G), 14(BP-RP)
    const std::vector<size_t> requiredIndices = {1, 2, 4, 6, 10, 14};

    // 3. Pre-validate: Check for "null" or empty strings BEFORE conversion
    for (size_t idx : requiredIndices) {
        const std::string& s = items[idx];
        if (s.empty() || s == "null") {
            return false; // Drop this star immediately
        }
    }

    double ra_degrees, dec_degrees, parallax;
    float g_mag, bp_rp;

    uint64_t sourceID = 0;

    try {
        sourceID = std::stoull(items[1]);
        ra_degrees = std::stod(items[2]);
        dec_degrees = std::stod(items[4]);
        parallax = std::stod(items[6]);
        g_mag = std::stof(items[10]);
        bp_rp = std::stof(items[14]);
    } catch (const std::exception& e) {
        for(size_t i = 0; i < items.size(); i++) {
            std::cout << i << "\t" << items[i] << std::endl;
        }
        std::cout << sourceID << std::endl;
        std::cerr << "Error parsing star data: " << e.what() << std::endl;
        exit(0);
        return false;
    }

    star.mMagnitude = g_mag;

    // Convert RA/Dec to radians (matching Cosmoscout formula)
    double ascension = (360.0 + 90.0 - ra_degrees) / 180.0 * static_cast<double>(GLM_PI);
    double declination = dec_degrees / 180.0 * static_cast<double>(GLM_PI);

    // Calculate effective temperature from color
    star.mTEff = calculateTEffFromColor(bp_rp);

    // Convert to Cartesian position (XYZ in parsecs)
    star.mPosition = sphericalToCartesian(ascension, declination, parallax);

    //star.mSourceID = sourceID;

    /*if(sourceID == 5945983304750985088) {
        std::cout << "found!" << std::endl;

        for(auto x : items) {
            std::cout << x << std::endl;
        }
        std::cout
        << star.mSourceID << "\t"
        << std::setprecision(64)
        << star.mTEff << "\t"
        << star.mMagnitude << "\t"
        << glm::to_string(star.mPosition)
        << std::endl;

        std::cout << std::endl;
        std::cout << ascension << std::endl;
        std::cout << declination << std::endl;
        std::cout << parallax << std::endl;
        exit(0);
    }*/

    return true;
}

class BinarySerializer {
private:
    std::vector<char> buffer;

public:
    void WriteInt32(int32_t value) {
        buffer.insert(buffer.end(), 
            reinterpret_cast<const char*>(&value),
            reinterpret_cast<const char*>(&value) + sizeof(int32_t));
    }

    void WriteUint32(uint32_t value) {
        buffer.insert(buffer.end(), 
            reinterpret_cast<const char*>(&value),
            reinterpret_cast<const char*>(&value) + sizeof(uint32_t));
    }

    void WriteFloat32(float value) {
        buffer.insert(buffer.end(),
            reinterpret_cast<const char*>(&value),
            reinterpret_cast<const char*>(&value) + sizeof(float));
    }

    void WriteFloat64(double value) {
        buffer.insert(buffer.end(),
            reinterpret_cast<const char*>(&value),
            reinterpret_cast<const char*>(&value) + sizeof(double));
    }

    const std::vector<char>& GetBuffer() const { return buffer; }
    size_t GetBufferSize() const { return buffer.size(); }
};

class BinaryDeserializer {
private:
    const char* data = nullptr;
    size_t size = 0;
    size_t position = 0;

public:
    void SetBuffer(const char* buffer, size_t size) {
        data = buffer;
        this->size = size;
        position = 0;
    }

    bool ReadInt32(int32_t& value) {
        if (position + sizeof(int32_t) > size) return false;
        value = *reinterpret_cast<const int32_t*>(&data[position]);
        position += sizeof(int32_t);
        return true;
    }

    bool ReadUint32(uint32_t& value) {
        if (position + sizeof(uint32_t) > size) return false;
        value = *reinterpret_cast<const uint32_t*>(&data[position]);
        position += sizeof(uint32_t);
        return true;
    }

    bool ReadFloat32(float& value) {
        if (position + sizeof(float) > size) return false;
        value = *reinterpret_cast<const float*>(&data[position]);
        position += sizeof(float);
        return true;
    }

    bool ReadFloat64(double& value) {
        if (position + sizeof(double) > size) return false;
        value = *reinterpret_cast<const double*>(&data[position]);
        position += sizeof(double);
        return true;
    }
};

} //namespace

namespace stars {

bool loadFromGaiaCSV(const std::string& filename) {
    std::vector<RawStar> stars;

    std::ifstream file;
    file.open(filename.c_str(), std::ifstream::in);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open catalog file: " << filename << std::endl;
        return false;
    }

    std::string line;
    int32_t lineCount = 0;

    // Skip header line
    if (!std::getline(file, line)) {
        std::cerr << "Empty file: " << filename << std::endl;
        return false;
    }

    bool success = true;
    size_t initialCount = stars.size();

    while (std::getline(file, line)) {
        ++lineCount;
        
        if (line.empty()) {
            continue;
        }
        
        std::vector<std::string> items = splitString(line, ',');
        
        if (items.size() >= 7) {
            RawStar star;
            success = parseGaiaLine(items, star);
            
            if (success) {
                stars.emplace_back(star);
                
                if (stars.size() % 10000 == 0) {
                    std::cout << "Read " << stars.size() << " stars so far..." << std::endl;
                }
            }
        }
    }

    file.close();
    
    std::cout << "Read a total of " << stars.size() << " stars (from " << initialCount << ")." << std::endl;
    std::cout << "Writing them to cache" << std::endl;
    writeStarCache(filename + ".cache", stars);

    return true;
}

// === BINARY CACHING ===

void writeStarCache(const std::string& filename, std::vector<RawStar> const& in) {
    BinarySerializer serializer;
    
    // Write header info
    serializer.WriteInt32(CACHE_VERSION);      // Cache version
    serializer.WriteInt32(static_cast<int32_t>(in.size()));
    
    // Write star data
    for (const auto& star : in) {
        serializer.WriteFloat32(star.mMagnitude);
        serializer.WriteFloat32(star.mTEff);

        serializer.WriteFloat64(star.mPosition.x);
        serializer.WriteFloat64(star.mPosition.y);
        serializer.WriteFloat64(star.mPosition.z);
    }

    std::ofstream file;
    file.open(filename.c_str(), std::ios::out | std::ios::binary);
    
    if (file.is_open()) {
        std::cout << "Writing " << in.size() << " stars (" 
                  << serializer.GetBufferSize() << " bytes) into '" 
                  << filename << "'." << std::endl;
        file.write(serializer.GetBuffer().data(), serializer.GetBufferSize());
        file.close();
    } else {
        std::cerr << "Failed to write binary star data: Cannot open file '" 
                  << filename << "' for writing!" << std::endl;
    }
}

inline glm::vec3 icrsToGalactic(const glm::vec3& v) {
    // Column-major: each glm::vec3 here is a COLUMN of the rotation matrix.

    return R_icrs_to_gal * v;
}

bool readStarCache(const std::string& filename, std::vector<RawStar>& out, bool transform_ICRS_to_galactic) {
    //out.clear();

    std::ifstream file;
    file.open(filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open cache file: " << filename << std::endl;
        return false;
    }

    int32_t size = static_cast<int32_t>(file.tellg());
    std::vector<char> data(static_cast<size_t>(size));
    
    file.seekg(0, std::ios::beg);
    file.read(data.data(), size);
    file.close();

    BinaryDeserializer deserializer;
    deserializer.SetBuffer(data.data(), static_cast<size_t>(size));

    int32_t cacheVersion = 0;
    int32_t numStars = 0;

    // Read header
    if (!deserializer.ReadInt32(cacheVersion) ||
        !deserializer.ReadInt32(numStars)) {
        std::cerr << "Failed to read cache header from '" << filename << "'" << std::endl;
        return false;
    }

    if (cacheVersion != CACHE_VERSION) {
        std::cerr << "Cache version mismatch: expected " << CACHE_VERSION 
                  << ", got " << cacheVersion << std::endl;
        return false;
    }

    // Read star data
    for (int32_t i = 0; i < numStars; ++i) {
        RawStar star;
        if (!deserializer.ReadFloat32(star.mMagnitude) ||
            !deserializer.ReadFloat32(star.mTEff)) {
            std::cerr << "Failed to read star data at index " << i << std::endl;
            return false;
        }

        //if(star.mMagnitude > 4.f) continue;
        
        // Read position components
        if (!deserializer.ReadFloat64(star.mPosition.x) ||
            !deserializer.ReadFloat64(star.mPosition.y) ||
            !deserializer.ReadFloat64(star.mPosition.z)) {
            std::cerr << "Failed to read position data at index " << i << std::endl;
            return false;
        }
        //static size_t ctr = 0;

        if(
            !(-10.f < star.mMagnitude && star.mMagnitude < 15.f)
            //|| ctr++ % 10 < 9
        ) {
            continue;
        }

        if(transform_ICRS_to_galactic) {
            star.mPosition = icrsToGalactic(star.mPosition);
        }
        
        out.emplace_back(star);
        
        if (out.size() % 1'000'000 == 0) {
            std::cout << "Read " << out.size() << " stars so far..." << std::endl;
        }
    }

    std::cout << "Read a total of " << out.size() << " stars from cache." << std::endl;
    return true;
}

} //namespace stars