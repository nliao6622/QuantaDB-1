/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 * All rights are reserved.
 */


#include "ClusterTimeService.h"
#include "Sequencer.h"

namespace DSSN {

Sequencer::Sequencer() { }

// Return CTS
uint64_t Sequencer::getCTS()
{
    return clock.getClusterTime(SEQUENCER_DELTA);
}

} // DSSN

