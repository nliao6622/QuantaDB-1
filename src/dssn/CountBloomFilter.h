/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 * All rights are reserved.
 */

#ifndef COUNT_BLOOM_FILTER_H
#define COUNT_BLOOM_FILTER_H

#include "Common.h"

namespace DSSN {

typedef uint8_t T;
const uint32_t BFSize = 65536;

/**
 * Counting Bloom Filter
 *
 * It must protect itself from overflowing any counter.
 * For now, only use two hash functions.
 * It supports one incrementer thread and one or more decrementer threads,
 * and those two can be the same one.
 *
 * LATER: may have caller to provide idx1 and idx2 if the caller pre-calculates and stores hash values
 */
class CountBloomFilter {
    PROTECTED:
    std::atomic<uint8_t> counters[BFSize];
    CountBloomFilter& operator=(const CountBloomFilter&);

    PUBLIC:
    // false if the key is failed to be added due to overflow
    bool add(const T *key, uint32_t size);

    // for performance, the key is assumed to have been added
    bool remove(const T *key, uint32_t size);

    bool contains(const T *key, uint32_t size);

    bool clear();

    void createIndexesFromKey(const T *key, uint32_t size, uint64_t *idx1, uint64_t *idx2);

}; // end CountBloomFilter class

} // end namespace DSSN

#endif  /* COUNT_BLOOM_FILTER_H */
