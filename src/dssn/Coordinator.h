/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 * All rights are reserved.
 */

#ifndef COORDINATOR_H
#define COORDINATOR_H

#include "Common.h"
#include "KVStore.h"
#include "Buffer.h"

namespace DSSN {

typedef RAMCloud::Buffer Buffer;

/**
 * Coordinator instance is used as a lib, tracking one transaction at a time for its client,
 * as the initiator of the DSSN commit protocol.
 *
 * This is the DSSN-equivalent of the RAMCloud::Transaction class.
 *
 * It does early-abort by performing SSN exclusion check upon each read operation.
 *
 * There are two options of how a write is handled. The first option is to skip sending
 * write op RPC to the validator and just pass the write set at commit intent. Then,
 * there is not early abort puon write op. The second option is to to send write RPC
 * to the validator though the write value would not matter and be cached in validator.
 * The meta data returned by the validator would help early abort.

 * It makes the read set non-overlapping with the write set.
 * It uses its sequencer to get a CTS before initiating the commit-intent.
 * It partitions the read set and write set according to the relevant shards
 * and initiates commit-intent(s) to relevant validator(s).
 *
 */
class Coordinator {
    PROTECTED:
    //DSSN data
    uint64_t cts; //commit time-stamp, globally unique
    uint64_t eta;
    uint64_t pi;

    uint32_t txState;
    std::set<KVLayout> readSet;
    std::set<KVLayout> writeSet;

    inline bool isExclusionViolated() { return pi <= eta; }

    bool ssnRead(KVLayout& kv, uint64_t cStamp, uint64_t sStamp) {
    	if (writeSet.find(kv) != writeSet.end()) {
    		eta = std::max(eta, cStamp);
    		if (sStamp == 0xffffffffffffffff) {
    			readSet.insert(kv);
    		} else {
    			pi = std::min(pi, sStamp);
    		}
    	}
    	return isExclusionViolated();
    }

    bool ssnWrite(KVLayout& kv, uint64_t pStampPrev) {
    	if (writeSet.find(kv) != writeSet.end()) {
    		eta = std::max(eta, pStampPrev);
    		writeSet.insert(kv);
    		readSet.erase(kv);
    	}
    	return isExclusionViolated();
    }

    PUBLIC:
    Coordinator();

    bool commit();

    void read(uint64_t tableId, const void* key, uint16_t keyLength,
            Buffer* value, bool* objectExists = NULL);

    void remove(uint64_t tableId, const void* key, uint16_t keyLength);

    void write(uint64_t tableId, const void* key, uint16_t keyLength,
            const void* buf, uint32_t length);

}; // end Coordinator class

} // end namespace DSSN

#endif  /* COORDINATOR_H */

