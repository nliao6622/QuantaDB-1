/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 * All rights are reserved.
 */

#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "Common.h"
#include "Object.h"
#include "TXEntry.h"
#include "HOTKV.h"

namespace DSSN {

typedef RAMCloud::Object Object;
typedef RAMCloud::KeyLength KeyLength;

/**
 * Supposedly one Validator instance per storage node, to handle DSSN validation.
 * It contains a pool of worker threads. They monitor the cross-shard
 * commit-intent (CI) queue and the local commit-intent queue and drain the commit-intents
 * (CI) from the queues. Each commit-intent is subject to DSSN validation.
 *
 */
class Validator {
    PROTECTED:

    // all operations about tuple store
    static HOTKV tupleStore;
    static uint64_t getTupleEta(Object& object);
    static uint64_t getTuplePi(Object& object);
    static uint64_t getTuplePrevEta(Object& object);
    static uint64_t getTuplePrevPi(Object& object);
    static bool maximizeTupleEta(Object& object, uint64_t eta);
    static bool updateTuple(Object& object, TXEntry& txEntry);

    bool updateTxEtaPi(TXEntry& txEntry);
    bool updateReadsetEta(TXEntry& txEntry);
    bool updateWriteset(TXEntry& txEntry);

    PUBLIC:
    Validator();
    bool validate(TXEntry& txEntry);

}; // end Validator class

} // end namespace DSSN

#endif  /* VALIDATOR_H */

