#pragma once

#include <cstdint>
#include <vector>
#include <RawStar.hpp>

#include "gpu_datatypes.hpp"

namespace newstar {

namespace preprocessing {

inline constexpr double CHUNK_SIZE_PARSECS = 12000.0 / 64; // 64^3 chunks, spanning 12^3 kpc^3, 12000 / 64 = 187.5
void run(
    std::vector<RawStar> const& rawStarsIn,
    std::vector<GpuChunkMeta>& gpuChunkMetasOut,
    std::vector<uint32_t>& gpuBatchRowsFlatOut,
    std::vector<uint32_t>& gpuBatchRowsHighPrecisionFlatOut
);
} //namespace preprocessing
} //namespace newstar