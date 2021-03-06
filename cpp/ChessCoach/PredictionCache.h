// ChessCoach, a neural network-based chess engine capable of natural-language commentary
// Copyright 2021 Chris Butner
//
// ChessCoach is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ChessCoach is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with ChessCoach. If not, see <https://www.gnu.org/licenses/>.

#ifndef _PREDICTIONCACHE_H_
#define _PREDICTIONCACHE_H_

#include <vector>

#include <Stockfish/types.h>

#include "Network.h"
#include "Platform.h"

struct alignas(128) PredictionCacheEntry
{
    // Positions with more moves don't fit in the cache and so shouldn't be probed/stored.
    static const int MaxMoveCount = 56;

    Key key;                                            // 8 bytes
    float value;                                        // 4 bytes
    int age;                                            // 4 bytes
    std::array<uint16_t, MaxMoveCount> policyPriors;    // 112 bytes
};
static_assert(sizeof(PredictionCacheEntry) == 128);

struct alignas(1024) PredictionCacheChunk
{
public:

    void Put(Key key, float value, int moveCount, const uint16_t* priors);

private:

    void Clear();
    bool TryGet(Key key, int moveCount, float* valueOut, uint16_t* priorsOut);

private:

    static const int EntryCount = 8;

    std::array<PredictionCacheEntry, EntryCount> _entries;  // 1024 bytes

    friend class PredictionCache;
};

static_assert(sizeof(PredictionCacheChunk) == 1024);

class PredictionCache
{
public:

    static PredictionCache Instance;

private:

    constexpr static const int MaxTableCount = (1 << 8);
    constexpr static const int MaxChunksPerTable = (1 << 20);

public:

    PredictionCache();
    ~PredictionCache();

    void Allocate(int sizeMebibytes);
    void Free();

    bool TryGetPrediction(Key key, int moveCount, PredictionCacheChunk** chunkOut, float* valueOut, uint16_t* priorsOut);
    void Clear();
    void ResetProbeMetrics();

    void PrintDebugInfo();
    int PermilleFull();
    int PermilleHits();
    int PermilleEvictions();

private:

    int _allocatedSizeMebibytes;
    std::vector<PredictionCacheChunk*> _tables;
    std::vector<void*> _allocations;
    std::vector<void*> _fallbackAllocations;
    int _chunksPerTable;

    uint64_t _hitCount;
    uint64_t _evictionCount;
    uint64_t _probeCount;

    uint64_t _entryCount;
    uint64_t _entryCapacity;

    friend struct PredictionCacheChunk;
};

#endif // _PREDICTIONCACHE_H_