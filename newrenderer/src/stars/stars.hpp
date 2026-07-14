#pragma once

#include <string>
#include <vector>

#include <RawStar.hpp>

// stars i/o (standalone, Cosmoscout-compatible functionality)
namespace stars {

constexpr glm::mat3 R_icrs_to_gal(
    glm::vec3(-0.8734370902348850, -0.1980763734312015, -0.4448296299600112), // col 0
    glm::vec3(-0.0548755604162154, -0.8676661490190047,  0.4941094278755837), // col 1
    glm::vec3(-0.4838350155487132,  0.4559837761750669,  0.7469822444972189)  // col 2
);
    /**
 * Load star catalog from Gaia CSV file (pipe-delimited)
 */
bool loadFromGaiaCSV(const std::string& filename);

/**
 * Write binary cache of loaded stars (call after successful CSV load)
 */
void writeStarCache(const std::string& filename, std::vector<RawStar> const& in);

/**
 * Read star data from binary cache
 */
bool readStarCache(const std::string& filename, std::vector<RawStar>& out, bool transform_ICRS_to_galactic = false);
};