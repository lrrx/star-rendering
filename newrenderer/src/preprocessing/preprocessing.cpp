#include "preprocessing.hpp"

#include <cstdlib>
#include <unordered_map>
#include <iostream>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>

#include <RawStar.hpp>
#include "preprocessing/datatypes.hpp"
#include "preprocessing/gpu_datatypes.hpp"
#include "util/util.hpp"

namespace {

using newstar::preprocessing::CHUNK_SIZE_PARSECS;

static inline size_t starsOOB = 0;

void bin_stars_to_chunks(std::vector<RawStar> const& raw_stars, std::unordered_map<uint32_t, RawChunk>& chunksOut) {
    std::cout << "global position of first raw star: " << glm::to_string(raw_stars.front().mPosition) << std::endl;
    std::cout << "number of input raw rawStars:" << raw_stars.size() << std::endl;
    
    //store to internal chunks first, copy out later while merging overlapping rawStars
    std::unordered_map<uint32_t, RawChunk> chunksInternal;
    
    for(RawStar const& raw_star : raw_stars) {
        glm::vec3 const globalChunkSpacePosition = raw_star.mPosition / CHUNK_SIZE_PARSECS;
        //if(glm::length(globalChunkSpacePosition) > 50.0) continue; //just for testing, this was useful to confirm that simply drawlist ssbo gets overloaded
        //if(rand() % 5 < 4) continue;
        if(glm::length(globalChunkSpacePosition) < 0.0001) continue; //filter out "erroneous"(?) stars very close to center landing on 0,0,0, causing perf. spike

        glm::vec3 const chunkCoordFloored = glm::floor(globalChunkSpacePosition);
        glm::ivec3 const chunkCoord{chunkCoordFloored};
        
        uint32_t chunkCoordEncoded = util::packIvec8(chunkCoord);
        if(chunkCoordEncoded == 0xffffffff) {
            starsOOB++;
            continue;
        }
        
        if(chunksInternal.find(chunkCoordEncoded) == chunksInternal.end()) {
            chunksInternal[chunkCoordEncoded].globalPosEncoded = chunkCoordEncoded;
            chunksInternal[chunkCoordEncoded].globalPos = globalChunkSpacePosition;
        }
        
        RawStar raw_star_local = raw_star;
        //highlight a certain area of stars for visual accuracy test
        //if(glm::length(globalChunkSpacePosition + 5.3f) < .25f) raw_star_local.mMagnitude = -10.0;

        //BUG
        //TODO: verify fract logic (at some point we got -1.0 to 1.0 here?)
        raw_star_local.mPosition = globalChunkSpacePosition - chunkCoordFloored;
        
        chunksInternal[chunkCoordEncoded].stars.emplace_back(raw_star_local);
    }
    
    chunksOut = chunksInternal;
}

} // namespace

namespace newstar {

namespace preprocessing {

void run(std::vector<RawStar> const& rawStarsIn,
    std::vector<GpuChunkMeta>& gpuChunkMetasOut,
    std::vector<uint32_t>& gpuBatchRowsFlatOut,
    std::vector<uint32_t>& gpuBatchRowsHighPrecisionFlatOut
) {
    std::unordered_map<uint32_t, RawChunk> chunks;


    std::cout << "rawStars.size() " << rawStarsIn.size() << std::endl;
    
    /*for(auto const& rawStar: rawStarsIn) {
        std::cout << rawStar.mMagnitude << "\t" << rawStar.mTEff << "\t" << glm::to_string(rawStar.mPosition) << std::endl;
    }*/

    bin_stars_to_chunks(rawStarsIn, chunks);

    size_t stars_in_chunks = 0;
    for(auto const& chunk : chunks) {
        stars_in_chunks += chunk.second.stars.size();
    }
    std::cout << "stars_in_chunks: " << stars_in_chunks + starsOOB << std::endl; 

    std::cout << "total number of chunks: " << chunks.size() << " chunks" << std::endl;

    gpuChunkMetasOut.reserve(chunks.size());

    std::unordered_map<uint32_t, RawChunk> leftoversChunks;
    std::unordered_map<uint32_t, RawChunk> sparseChunks;

    std::vector<GpuBatchRow> gpuBatchRows;
    std::vector<GpuBatchRow> gpuBatchRowsHighPrecision;
    //copy delta rawStars from chunks sequentially, pass global offset ptr to chunk
    for(auto const& [id, chunk] : chunks) {
        if(chunk.stars.size() < 64) {
            glm::ivec3 posDec = util::unpackIvec8(chunk.globalPosEncoded);
            glm::ivec3 mod16 = ((posDec % 16) + 16) % 16;
            glm::ivec3 globPos = (posDec - mod16) / 16;
            uint32_t globPosEnc = util::packIvec8(globPos);

            for(auto const& star : chunk.stars) {
                sparseChunks[globPosEnc].globalPosEncoded = globPosEnc;
                sparseChunks[globPosEnc].stars.push_back(star);
                sparseChunks[globPosEnc].stars.back().mPosition /= 16.0;
                sparseChunks[globPosEnc].stars.back().mPosition += glm::dvec3(mod16) / 16.0;
            }

            continue;
        }

        //REVEAL(waste of memory fetches if preprocessing doesnt redistribute padding stars into less dense chunks, see rasterize.comp PADDING_STAR=0xffffffff side too)

        size_t starCountAligned = chunk.stars.size() - chunk.stars.size() % GPU_THREAD_COUNT;
        QuantizedChunk quantizedChunk{chunk.stars, starCountAligned, chunk.globalPosEncoded, 1};
        quantizedChunk.gpuSerialize(gpuBatchRows, gpuBatchRowsHighPrecision, gpuChunkMetasOut);

        glm::ivec3 globalPosDecoded = util::unpackIvec8(chunk.globalPosEncoded);
        glm::ivec3 mod4 = ((globalPosDecoded % 4) + 4) % 4;
        glm::ivec3 globalPosSparse = (globalPosDecoded - mod4) / 4;
        uint32_t globalPosSparseEncoded = util::packIvec8(globalPosSparse);

        //std::cout << globalPosSparseEncoded << std::endl;
        for(auto it = chunk.stars.begin() + starCountAligned; it != chunk.stars.end(); it++) {     
            leftoversChunks[globalPosSparseEncoded].globalPosEncoded = globalPosSparseEncoded;
            leftoversChunks[globalPosSparseEncoded].stars.push_back(*it);
            leftoversChunks[globalPosSparseEncoded].stars.back().mPosition /= 4.0;
            leftoversChunks[globalPosSparseEncoded].stars.back().mPosition += glm::dvec3(mod4) / 4.0;
        }
    }

    for(auto const& [id, chunk] : leftoversChunks) {
        QuantizedChunk quantizedChunk{chunk.stars, chunk.stars.size(), chunk.globalPosEncoded, 4};
        quantizedChunk.gpuSerialize(gpuBatchRows, gpuBatchRowsHighPrecision, gpuChunkMetasOut);
        //std::cout << id << "\t" << chunk.stars.size() << std::endl;
    }

      for(auto const& [id, chunk] : sparseChunks) {
        QuantizedChunk quantizedChunk{chunk.stars, chunk.stars.size(), chunk.globalPosEncoded, 16};
        quantizedChunk.gpuSerialize(gpuBatchRows, gpuBatchRowsHighPrecision, gpuChunkMetasOut);
        //std::cout << id << "\t" << chunk.stars.size() << std::endl;
    }

    for(auto const& gpuBatchRow : gpuBatchRows) {
        //serialize main batch table
        for(size_t i = 0; i < GPU_THREAD_COUNT; i++) {
            gpuBatchRowsFlatOut.push_back(gpuBatchRow[i * 2 + 0]);
            gpuBatchRowsFlatOut.push_back(gpuBatchRow[i * 2 + 1]);
        }
    }

    for(auto const& rowHighP : gpuBatchRowsHighPrecision) {
        //serialize main batch table
        for(size_t i = 0; i < GPU_THREAD_COUNT; i++) {
            gpuBatchRowsHighPrecisionFlatOut.push_back(rowHighP[i]);
        }
    }

    size_t non_zero_entries = 0;

    for(auto const& row : gpuBatchRowsHighPrecision) {
        for(auto const& x : row) {
            if(x != 0) non_zero_entries++;
        }
    }

    size_t padding_stars_count = 0;

    for(auto const& row : gpuBatchRows) {
        for(auto const& x : row) {
            if(x == 0xFFFFFFFFu) padding_stars_count++;
        }
    }

    std::cout << "padding_stars_count" << padding_stars_count << std::endl;
    std::cout << "non_zero_entries in gpu rows" << non_zero_entries << std::endl;
    //std::quick_exit(0);
    //std::quick_exit(0);

    //std::cout << "minMag:" << minMag << std::endl;
    //std::cout << "maxMag:" << maxMag << std::endl;
}

} //namespace preprocessing
} //namespace newstar