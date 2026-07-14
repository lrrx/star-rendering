#include "datatypes.hpp"

#include <algorithm>
#include <vector>
#include <glm/glm.hpp>
#include "gpu_datatypes.hpp"
#include <iostream>

namespace {

// Combine stars sharing an encoded position into one point source.
//
// Precondition: `sorted` is ordered so entries with equal `.pos` are adjacent
// (sorting by `.pos`, as you already do, satisfies this).
//
// Magnitude is logarithmic: m = -2.5*log10(F). Sources combine by summing
// *flux*, with a flux-weighted mean colour. Stars in one quantization cell are
// at essentially the same distance, so summed flux is brightness-preserving for
// the renderer. emit() is called once per distinct position.
template <typename EmitFn>
void mergeStarsByPosition(std::vector<QuantizedStar> const& sorted, EmitFn&& emit) {
    if (sorted.empty()) return;
    
    /*auto magToFlux = [](float mag) -> double {
        return std::pow(10.0, -0.4 * static_cast<double>(mag));
    };
    
    uint32_t accPos   = sorted[0].pos;
    double   accFlux  = magToFlux(sorted[0].mag);          // Σ flux
    double   accFluxT = accFlux * double(sorted[0].temp);  // Σ flux·temp
    
    auto flush = [&]() {
        float mag  = (accFlux > 0.0) ? float(-2.5 * std::log10(accFlux)) : 99.0f;
        float temp = (accFlux > 0.0) ? float(accFluxT / accFlux)         : 0.0f;
        emit(accPos, mag, temp);
    };*/
    
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        QuantizedStar const& s = sorted[i];
        /*if (s.pos == accPos) {
            double f  = magToFlux(s.mag);
            accFlux  += f;
            accFluxT += f * double(s.temp);
        } else {
            flush();
            accPos   = s.pos;
            accFlux  = magToFlux(s.mag);
            accFluxT = accFlux * double(s.temp);
        }*/
        emit(s.coord.lowp, s.coord.midp, s.mag, s.temp);
    }
    //flush();
}
    
//BUG: a problem here (before moving back to non-delta encoded positions) was perhaps that we cacluate row count before accounting for rawStars dropped later?
//fixed in this impl, but remember to re-iterate on the old logic if moving back to deltas

//#define USE_OLD_STAR_MERGING

static float minMag = std::numeric_limits<float>::max();
static float maxMag = std::numeric_limits<float>::min();

uint16_t encodeMag(float mag) {
    assert(-10.f < mag && mag < 20.f);
    
    float encodedFloat = (mag + 10.f) / 30.f * static_cast<float>(0xffff);
    
    if(mag < minMag) minMag = mag;
    if(mag > maxMag) maxMag = mag;
    return static_cast<uint16_t>(encodedFloat);
};

uint16_t encodeTemp(float temp) {
    if(!(0.f <= temp && temp < 60'000.f)){
        std::cout << "WARNING: temp outside range:" << temp << std::endl;
    }
    temp = glm::clamp(temp, 0.f, 59'999.f);
    assert (0.f <= temp && temp < 60'000.f);
    return static_cast<uint16_t>(temp);
};

} // namespace

QuantizedChunk::QuantizedChunk(std::vector<RawStar> const& stars, size_t starCount, uint32_t _globalPosEncoded, uint8_t _size) {
    this->globalPosEncoded = _globalPosEncoded;
    this->size = _size;

    size_t i = 0;

    for(auto const& rawStar : stars) {
        if(i> starCount) break;
        i++;
        quantizedStars.emplace_back(rawStar.mPosition, rawStar.mMagnitude, rawStar.mTEff);
    }

    // sort rawStars by morton code, this is critical for delta encoding, but also helps with more coherent write patterns when iterating over them
    std::sort(quantizedStars.begin(), quantizedStars.end(),
    [](QuantizedStar const& a, QuantizedStar const& b) {
        return a.coord.lowp < b.coord.lowp; //TODO: implement proper "lexicographical" (or just keep original 24 bit per axis) order
    });
}

void QuantizedChunk::gpuSerialize(
    std::vector<GpuBatchRow>& batchRowsOut,
    std::vector<GpuBatchRow>& highPrecisionBatchRowsOut,
    std::vector<GpuChunkMeta>& chunkMetaOut
) {
    // Maximum rows any single GpuChunkMeta entry can carry. Heavy chunks are
    // split into N entries pointing at consecutive row ranges of the same
    // batchRowsOut block, all sharing the same chunkID. With MAX_JOB_CHUNKS
    // chunks-per-job on the GPU side, peak work per job is bounded by
    // MAX_CHUNK_ROWS * MAX_JOB_CHUNKS rows. Tune this against the GPU-side
    // MAX_JOB_CHUNKS define to dial in the load-balance grain.
    constexpr uint32_t MAX_CHUNK_ROWS = 4;

    constexpr uint32_t PADDING_STAR = 0xFFFFFFFFu;

    // Capture starting row index BEFORE emitting; we'll emit metas at the end.
    size_t const startRow = batchRowsOut.size();

    //actual serialization
    size_t writeIdx = 0; // running output star index, decoupled from input idx

    auto emitStar = [&](uint32_t pos, uint32_t posHighPrecision, float mag, float temp) {
        size_t thread = writeIdx % GPU_THREAD_COUNT;
        if (thread == 0) batchRowsOut.emplace_back();   // start new row on boundary
        GpuBatchRow& row = batchRowsOut.back();

        if (thread == 0) highPrecisionBatchRowsOut.emplace_back(); // start new row on boundary
        GpuBatchRow& rowHighPrecision = highPrecisionBatchRowsOut.back();

        // TODO: proper uint16_t <-> float mapping for mag/temp
        uint16_t magEnc = encodeMag(mag);
        uint16_t tempEnc = encodeTemp(temp);
        uint32_t magTemp = (uint32_t(magEnc) << 16) | uint32_t(tempEnc);

        row[thread * 2 + 0] = pos;
        row[thread * 2 + 1] = magTemp;
        rowHighPrecision[thread] = posHighPrecision;
        ++writeIdx;
    };

    mergeStarsByPosition(quantizedStars, emitStar);

    // Pad tail of last row up to the 256-thread boundary.
    while (writeIdx % GPU_THREAD_COUNT != 0) {
        size_t thread = writeIdx % GPU_THREAD_COUNT;
        GpuBatchRow& row = batchRowsOut.back();
        GpuBatchRow& rowHighPrecision = highPrecisionBatchRowsOut.back();
        row[thread * 2 + 0] = PADDING_STAR;
        row[thread * 2 + 1] = 0;
        rowHighPrecision[thread] = 0;
        ++writeIdx;
    }

    // Now emit one or more metas covering the rows we just wrote. The GPU
    // rasterizer treats each meta as an indivisible drawing unit, so splitting
    // here is what gives the binner a uniform-grained input.
    size_t const totalRows = batchRowsOut.size() - startRow; //TODO: make this cleaner (dont have 2 separate containers with presumed same size)
    if (totalRows == 0) return; // empty chunk: don't pollute the meta list

    uint32_t const packedChunkID =
        (this->globalPosEncoded & 0x00ffffffu) | ((uint32_t(this->size) << 24));

    for (size_t emitted = 0; emitted < totalRows; emitted += MAX_CHUNK_ROWS) {
        size_t const slice = std::min<size_t>(MAX_CHUNK_ROWS, totalRows - emitted);
        GpuChunkMeta& meta = chunkMetaOut.emplace_back();
        meta.chunkID     = packedChunkID;
        meta.startRowNum = uint32_t(startRow + emitted);
        meta.rowCount    = uint32_t(slice);
        meta.pad0        = 0;
    }
}