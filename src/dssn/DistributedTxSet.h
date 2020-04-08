
/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 * All rights are reserved.
 */

#ifndef DISTRIBUTEDTXSET_H
#define DISTRIBUTEDTXSET_H

#include "TxEntry.h"
#include "ActiveTxSet.h"
#include <boost/lockfree/spsc_queue.hpp>
#include "CountBloomFilter.h"
#include "WaitList.h"

namespace DSSN {

const uint32_t independentQueueSize = 65536;
const uint32_t coldDependQueueSize = 65536;
const uint32_t hotDependQueueSize = 1000000;

/*
 * The class contains some due cross-shard CIs that are to feed activeTxSet.
 * It sits after the reorderQueue and before the activeTxSet in the validation pipeline.
 *
 * Expect one consumer and one producer.
 *
 * It contains one independent queue, one cold dependent queue, and one hot dependent queue.
 * Each queue has an associated counting Bloom Filter (CBF).
 *
 * A CI that does not depend on earlier CIs is appended to the independent queue.
 * A CI that depends on earlier CIs is appended to the cold dependent queue or the hot
 * dependent queue, to the latter if the CI shows a long dependency graph, indicated by
 * the returned count of the cold CBF exceeding a threshold.
 *
 * CIs in the independent queue can be moved into the activeTxSet (if it is not blocked
 * by the activeTxSet) in any order, i.e., jumping queue is allowed.
 *
 * CIs in the cold (dependent) queue can be moved into the activeTxSet only after earlier CIs in
 * the cold queue and the independent queue have been moved.
 *
 * CIs in the hot (dependent) queue can be moved into the activeTxSet only after earlier CIs in
 * the hot queue, cold queue, and independent queue have been moved.
 *
 *
 */
class DistributedTxSet {
	PROTECTED:

	//WaitList independentQueue ;
	WaitList independentQueue{independentQueueSize};
	WaitList coldDependQueue{coldDependQueueSize};
	WaitList hotDependQueue{hotDependQueueSize};

	CountBloomFilter<uint8_t> independentCBF = CountBloomFilter<uint8_t>((1 << 18), 255);
	CountBloomFilter<uint8_t> coldDependCBF = CountBloomFilter<uint8_t>((1 << 15), 255);
	CountBloomFilter<uint32_t> hotDependCBF = CountBloomFilter<uint32_t>((1 << 10), 100000);

	uint64_t lastColdDependCTS = 0;
	uint64_t lastIndependentCTS = 0;

	uint32_t hotThreshold = 255;

	//for performance optimization
	uint64_t activitySignature = -1;
	uint64_t addedTxCount = 0;
	uint64_t removedTxCount = 0;

	template <class T> inline bool dependsOnEarlierTxs(T &cbf, TxEntry *txEntry, uint32_t &count);
	template <class T> inline bool addToCBF(T &cbf, TxEntry *txEntry);
	inline bool addToHotTxs(TxEntry *txEntry);
	inline bool addToColdTxs(TxEntry *txEntry);
	inline bool addToIndependentTxs(TxEntry *txEntry);

    PUBLIC:
    // return true if the CI is added successfully
    bool add(TxEntry *txEntry);

    // return the CI that is not blocked by active tx set nor by any earlier CIs
    TxEntry* findReadyTx(ActiveTxSet &activeTxSet);

    uint32_t count() { return (addedTxCount - removedTxCount); } //Fixeme

    DistributedTxSet();

}; 

} // end namespace DSSN

#endif  // DISTRIBUTEDTXSET_H
