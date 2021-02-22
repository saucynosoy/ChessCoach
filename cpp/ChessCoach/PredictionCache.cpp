#include "PredictionCache.h"

#include <iostream>
#include <cassert>

#include "PoolAllocator.h"

PredictionCache PredictionCache::Instance;

void PredictionCacheChunk::Clear()
{
    for (int i = 0; i < EntryCount; i++)
    {
        _entries[i].key = 0;
        _entries[i].age = 0;
    }
}

bool PredictionCacheChunk::TryGet(Key key, int moveCount, float* valueOut, float* priorsOut)
{
    for (PredictionCacheEntry& entry : _entries)
    {
        entry.age++;
    }

    for (PredictionCacheEntry& entry : _entries)
    {
        if (entry.key == key)
        {
            // Various types of collisions and race conditions across threads are possible:
            //
            // - Key collisions (type-1 errors):
            //      - Can mitigate by increasing key size, table size, or e.g. for Stockfish,
            //        validating the stored information, a Move, against the probing position.
            //        Note that validating using key/input information rather than stored/output
            //        is equivalent to increasing key size. We can mitigate by summing policy for
            //        the probing legal move count and ensuring that it's nearly 1.0.
            // - Index collisions (type-2 errors):
            //      - Results from the bit shrinkage from the full key size down to the addressable
            //        space of the table. Can be mitigated by storing the the full key, or more of
            //        the key than the addressable space, and validating when probing. We store the
            //        full key.
            // - Spliced values from parallel thread writes:
            //      - Multiple threads may race to write to a single chunk and/or entry, splicing
            //        values - either intra-field (e.g. writing to different parts of the policy)
            //        or inter-field (e.g. one thread writing key/value and the other writing policy).
            //        We have to accept seeing an incorrect value, but intra and inter-policy splicing
            //        can potentially be detected by again summing policy for the probing legal move count
            //        and ensuring that it's nearly 1.0.
            //
            // Use the provided "priorsOut" as writable scratch space even if we return false.
            //
            // Most real probabilities will have canceling bias from quantization errors,
            // but uniform policy will give cases like 1/245 * 245, 1.0 vs. ~0.9982, 65535 vs. 65415,
            // and we may get unlucky on certain batches (don't actually need to cache pure uniform).
            //
            // Allow for 120 quanta error, ~0.18% (used to be much higher with 8-bit quantization, ~3.5% for 9).
            // This allows for the largest uniform error for legal moves in [1, 256].

            int priorSum = 0;
            for (int m = 0; m < moveCount; m++)
            {
                const uint16_t quantizedPrior = entry.policyPriors[m];
                priorSum += quantizedPrior;

                const float prior = INetwork::DequantizeProbabilityNoZero(quantizedPrior);
                priorsOut[m] = prior;
            }

            // Check for type-1 errors and splices and return false. It's important not to
            // freshen the age in these cases so that splices can be overwritten with good data.
            constexpr int expected = INetwork::QuantizeProbabilityNoZero(1.f);
            static_assert(INetwork::DequantizeProbabilityNoZero(expected) == 1.f);
            const int allowance = 120;
            if ((priorSum < (expected - allowance)) || (priorSum > (expected + allowance)))
            {
                return false;
            }

            // The entry is valid, as far as we can tell, so freshen its age and return it.
            entry.age = std::numeric_limits<int>::min();
            *valueOut = entry.value;
            return true;
        }
    }

    return false;
}

void PredictionCacheChunk::Put(Key key, float value, int moveCount, const float* priors)
{
    // If the same full key is found than that entry needs to be replaced so that
    // TryGet finds it. Otherwise, replace the oldest entry.
    int oldestIndex = 0;
    for (int i = 0; i < EntryCount; i++)
    {
        if (_entries[i].key == key)
        {
            oldestIndex = i;
            break;
        }
        if (_entries[i].age > _entries[oldestIndex].age)
        {
            oldestIndex = i;
        }
    }

    // Hackily reach into the singleton PredictionCache to update metrics.
    if (_entries[oldestIndex].key)
    {
        PredictionCache::Instance._evictionCount++;
    }
    else
    {
        PredictionCache::Instance._entryCount++;
    }

    _entries[oldestIndex].key = key;
    _entries[oldestIndex].value = value;
    _entries[oldestIndex].age = std::numeric_limits<int>::min();
    for (int m = 0; m < moveCount; m++)
    {
        _entries[oldestIndex].policyPriors[m] = INetwork::QuantizeProbabilityNoZero(priors[m]);
    }

    // Place a "guard" probability of 1.0 immediately after the N legal moves' probabilities
    // so that "TryGet" can more often detect incorrect probability sums (rather than potentially
    // seeing only trailing zeros and still summing to 1.0).
    //
    // This unfortunately won't help with all trailing zeros - e.g. placing 5 priors, {0.1, 0.2, 0.3, 0.4, 0.0},
    // and reading back 4 - but we shouldn't be placing actual quantized zeros (rounded up to one quantum) - just
    // have to worry about small values not triggering the quantization error allowance check.
    if (moveCount < _entries[oldestIndex].policyPriors.size())
    {
        _entries[oldestIndex].policyPriors[moveCount] = INetwork::QuantizeProbabilityNoZero(1.f);
    }
}

PredictionCache::PredictionCache()
    : _allocatedRequestGib(0)
    , _allocatedMinGib(0)
    , _chunksPerTable(0)
    , _hitCount(0)
    , _evictionCount(0)
    , _probeCount(0)
    , _entryCount(0)
    , _entryCapacity(0)
{
}

PredictionCache::~PredictionCache()
{
    Free();
}

void PredictionCache::Allocate(int requestGib, int minGib)
{
    // Require <= 256 GiB and a power of two, or zero.
    requestGib = std::max(requestGib, minGib);
    if (minGib < 0)
    {
        throw std::invalid_argument("Negative size");
    }
    if ((requestGib > MaxTableCount) || (minGib > MaxTableCount))
    {
        static_assert(MaxTableCount == 256);
        throw std::invalid_argument("Maximum prediction cache size 256 GiB");
    }
    if (((requestGib & (requestGib - 1)) != 0) || ((minGib & (minGib - 1)) != 0))
    {
        throw std::invalid_argument("Prediction cache size must be a power-of-two GiB or zero");
    }

    // Do nothing if already identically allocated.
    if ((requestGib == _allocatedRequestGib) && (minGib == _allocatedMinGib))
    {
        return;
    }

    // Allocate may be called on an already-allocated prediction cache (with different request/min).
    Free();

    // Try 1 GiB table allocations, then 512 MiB, 256 MiB, etc. until reaching maximum table count for the requested "requestGb"
    // (e.g. for 1 GiB prediction cache, minimum table size is 4 MiB; for 8 GiB prediction cache, minimum table size is 32 MiB).
    // Then, walk the "requestGb" down to "minGb" and repeat the table size walk-down for each size.
    static_assert(MaxChunksPerTable * sizeof(PredictionCacheChunk) == (1 << 30)); // Our largest tables are 1 GiB.
    static_assert(((1 << 30) / MaxTableCount) % sizeof(PredictionCacheChunk) == 0); // Our smallest tables still fit a whole number of chunks.
    int sizeGib = requestGib;
    int chunksPerTable = MaxChunksPerTable;
    while (true)
    {
        const int tableSizeBytes = chunksPerTable * sizeof(PredictionCacheChunk);
        const int tableCount = sizeGib * ((1 << 30) / tableSizeBytes);

        if (tableCount > MaxTableCount)
        {
            // Walked "chunksPerTable" down to the limit, all failed.
            if (sizeGib > minGib)
            {
                // Reduce the request (still above minimum size) and reset the walk-down.
                sizeGib >>= 1;
                chunksPerTable = MaxChunksPerTable;
                continue;
            }
            else
            {
                // Can't satisfy minimum size.
                throw std::runtime_error("Remaining memory is too fragmented to allocate prediction cache: close programs, reduce cache size or reboot");
            }
        }

        if (TryAllocate(tableSizeBytes, tableCount, chunksPerTable))
        {
            _allocatedRequestGib = requestGib;
            _allocatedMinGib = minGib;
            std::cout << "Allocated prediction cache, size=" << sizeGib << " GiB, tables=" << tableCount
                << " (request=" << requestGib << " GiB, min=" << minGib << " GiB)" << std::endl;
            break;
        }

        chunksPerTable >>= 1;
    }
}

bool PredictionCache::TryAllocate(int tableSizeBytes, int tableCount, int chunksPerTable)
{
    assert(tableCount <= MaxTableCount);
    assert(chunksPerTable <= MaxChunksPerTable);

    _tables.reserve(tableCount);

    for (int i = 0; i < tableCount; i++)
    {
        // Allocate table with large page support.
        void* memory = LargePageAllocator::Allocate(tableSizeBytes);
        if (!memory)
        {
            break;
        }

        _tables.push_back(reinterpret_cast<PredictionCacheChunk*>(memory));
    }

    // If any allocations failed, free the partial allocations before returning.
    if (_tables.size() < tableCount)
    {
        for (void* memory : _tables)
        {
            if (memory)
            {
                LargePageAllocator::Free(memory);
            }
        }
        _tables.clear();
        return false;
    }

    _chunksPerTable = chunksPerTable;
    _entryCapacity = (static_cast<uint64_t>(tableCount) * chunksPerTable * PredictionCacheChunk::EntryCount);

#ifdef CHESSCOACH_WINDOWS
    // Memory is already zero-filled by VirtualAlloc on Windows, so no need to clear chunks.
#else
    // Memory comes from std::aligned_alloc in Linux with madvise hint, so no zero-filling guarantees
    // and may not even be large pages. We can either Clear() or memset, haven't timed them.
    Clear();
#endif

    return true;
}

void PredictionCache::Free()
{
    _allocatedRequestGib = 0;
    _allocatedMinGib = 0;

    for (void* memory : _tables)
    {
        if (memory)
        {
            LargePageAllocator::Free(memory);
        }
    }
    _tables.clear();

    _chunksPerTable = 0;

    ResetProbeMetrics();

    _entryCount = 0;
    _entryCapacity = 0;
}

// If returning true, valueOut and priorsOut are populated; chunkOut is not populated.
// If returning false, valueOut is not populated; priorsOut may be clobbered; chunkOut is populated only if the value/policy should be stored when available.
bool PredictionCache::TryGetPrediction(Key key, int moveCount, PredictionCacheChunk** chunkOut, float* valueOut, float* priorsOut)
{
    if (_tables.empty())
    {
        return false;
    }

    _probeCount++;

    // We're not using very many bits of the key for our tables: currently 26 bits of entries, 23 bits of 64 used to index, 3 bits decided via age/linear scan.
    // Since hashes are Zobrist we can assume distribution is pretty even across different bit positions. However, since our modulos should be powers of two
    // we don't need to worry about bias and can cheaply xor to combine entropy (between 1x and 2x per combination, depending), with no discards necessary.

    // Use up to 16 high bits to choose the table (e.g. lowest 3 of the 16 for 8-GiB cache with 1-GiB tables).
    const uint16_t tableKey = (key >> 48);
    // Xor down to 8 bits (covers MaxTableCount, checked in "Allocate").
    static_assert(MaxTableCount == (1 << 8));
    const uint16_t tableKeyXor = ((tableKey & 0xFF) ^ (tableKey >> 8));
    PredictionCacheChunk* table = _tables[tableKeyXor % _tables.size()];

    // Use up to 48 low bits to choose the chunk (e.g. lowest 20 of the 48 for 1-GiB table with 1-MiB chunks).
    const uint64_t chunkKey = (key & 0xFFFFFFFFFFFF);
    // Xor lowest 40 bits down to 20 bits (covers MaxChunksPerTable, checked in "Allocate"); don't worry about bits dangling above.
    static_assert(MaxChunksPerTable == (1 << 20));
    const uint64_t chunkKeyXor = ((chunkKey & 0xFFFFF) ^ (chunkKey >> 20));
    PredictionCacheChunk& chunk = table[chunkKeyXor % _chunksPerTable];

    // Age-based differentiation among entries in the chunk covers another 3 bits' worth.
    if (chunk.TryGet(key, moveCount, valueOut, priorsOut))
    {
        _hitCount++;
        return true;
    }

    *chunkOut = &chunk;
    return false;
}

void PredictionCache::Clear()
{
    for (PredictionCacheChunk* table : _tables)
    {
        assert(_chunksPerTable > 0); // If there are _tables then _chunksPerTable shouldn't be zero.
        for (int i = 0; i < _chunksPerTable; i++)
        {
            table[i].Clear();
        }
    }

    ResetProbeMetrics();

    _entryCount = 0;
}

void PredictionCache::ResetProbeMetrics()
{
    _hitCount = 0;
    _evictionCount = 0;
    _probeCount = 0;
}

void PredictionCache::PrintDebugInfo()
{
    std::cout << "Prediction cache full: " << (static_cast<float>(_entryCount) / _entryCapacity)
        << ", hit rate: " << (static_cast<float>(_hitCount) / _probeCount) 
        << ", eviction rate: " << (static_cast<float>(_evictionCount) / _probeCount) << std::endl;
}

int PredictionCache::PermilleFull()
{
    return ((_entryCapacity == 0) ? 0 : static_cast<int>(_entryCount * 1000 / _entryCapacity));
}

int PredictionCache::PermilleHits()
{
    return ((_probeCount == 0) ? 0 : static_cast<int>(_hitCount * 1000 / _probeCount));
}

int PredictionCache::PermilleEvictions()
{
    return ((_probeCount == 0) ? 0 : static_cast<int>(_evictionCount * 1000 / _probeCount));
}