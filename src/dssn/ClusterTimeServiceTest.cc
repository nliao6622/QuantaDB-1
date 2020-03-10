/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 */

#include "TestUtil.h"
#include "ClusterTimeService.h"
#include "Cycles.h"

#define GTEST_COUT  std::cerr << "[ INFO ] "

namespace RAMCloud {

using namespace DSSN;

class ClusterTimeServiceTest : public ::testing::Test {
  public:
  ClusterTimeServiceTest() {};
  ~ClusterTimeServiceTest() {};

  ClusterTimeService clock;

  DISALLOW_COPY_AND_ASSIGN(ClusterTimeServiceTest);
};

TEST_F(ClusterTimeServiceTest, getClusterTime) {
    GTEST_COUT << "ClusterTimeServiceTest" << std::endl;
    for(int ii = 0; ii < 1000; ii++) {
        uint64_t t1 = clock.getClusterTime();
        uint64_t t2 = clock.getClusterTime();
        uint64_t t3 = clock.getClusterTime();
        uint64_t t4 = clock.getClusterTime();
        EXPECT_GT(t2, t1);
        EXPECT_GT(t3, t2);
        EXPECT_GT(t4, t3);
    }
}

TEST_F(ClusterTimeServiceTest, getLocalTime) {
    for(int ii = 0; ii < 1000; ii++) {
        uint64_t t1 = clock.getLocalTime();
        uint64_t t2 = clock.getLocalTime();
        uint64_t t3 = clock.getLocalTime();
        uint64_t t4 = clock.getLocalTime();
        EXPECT_GT(t2, t1);
        EXPECT_GT(t3, t2);
        EXPECT_GT(t4, t3);
    }
}

TEST_F(ClusterTimeServiceTest, benchGenClusterTime) {
    int loop = 1024*1024;
    uint64_t start, stop;
    //
    start = Cycles::rdtsc();
    for (int i = 0; i < loop; i++) {
        clock.getClusterTime();
    }
    stop = Cycles::rdtsc();
    GTEST_COUT << "getClusterTime: "
    << Cycles::toNanoseconds(stop - start)/(1024*1024) << " nano sec per call " << std::endl;

    //
    start = Cycles::rdtsc();
    for (int i = 0; i < loop; i++) {
        clock.getClusterTime(1000);
    }
    stop = Cycles::rdtsc();
    GTEST_COUT << "getClusterTime(delta): "
    << Cycles::toNanoseconds(stop - start)/(1024*1024) << " nano sec per call " << std::endl;

    //
    start = Cycles::rdtsc();
    for (int i = 0; i < loop; i++) {
        clock.getLocalTime();
    }
    stop = Cycles::rdtsc();
    GTEST_COUT << "getLocalTime: "
    << Cycles::toNanoseconds(stop - start)/(1024*1024) << " nano sec per call " << std::endl;

    //
    start = Cycles::rdtsc();
    for (int i = 0; i < loop; i++) {
        clock.Cluster2Local(start);
    }
    stop = Cycles::rdtsc();
    GTEST_COUT << "Cluster2Local: "
    << Cycles::toNanoseconds(stop - start)/(1024*1024) << " nano sec per call " << std::endl;

}
}  // namespace RAMCloud
