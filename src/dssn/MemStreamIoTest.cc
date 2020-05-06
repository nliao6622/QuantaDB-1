/*
 * Copyright (c) 2020  Futurewei Technologies, Inc.
 */
#include "TestUtil.h"
#include "Cycles.h"
#include "MemStreamIo.h"
#include "KVStore.h"
#include "TxEntry.h"

#define GTEST_COUT  std::cerr << "[ INFO ] "

namespace RAMCloud {

using namespace DSSN;

class MemStreamIoTest : public ::testing::Test {
  public:
  MemStreamIoTest() {};
  ~MemStreamIoTest() {};

  uint8_t buf[1024*1024];


  DISALLOW_COPY_AND_ASSIGN(MemStreamIoTest);
};

TEST_F(MemStreamIoTest, MemStreamIoUnitTest)
{
    // KLayout test
    KLayout k1(30), k2(30);
    GTEST_COUT << "MemStreamIoTest" << std::endl;

    const char *keystr = (const char *)"MemStreamIoTestKey";
    uint8_t *valstr = const_cast<uint8_t *>((const uint8_t*)"MemStreamIoTestValue");

    memcpy(k1.key.get(), keystr, strlen(keystr));
    outMemStream out(buf, sizeof(buf));
    k1.serialize(out);

    inMemStream in(buf, out.dsize());
    k2.deSerialize( in );
    EXPECT_EQ(k1, k2);

    // KVLayout
    KVLayout kv1(30), kv2(30);
    memcpy(kv1.k.key.get(), keystr, strlen(keystr));
    kv1.v.valuePtr = valstr;
    kv1.v.valueLength = strlen((char *)valstr);
    kv1.v.meta.pStamp = 0xF0F0F0F0;
    kv1.v.isTombstone = true;

    outMemStream out1(buf, sizeof(buf));
    kv1.serialize( out1 );

    inMemStream in1(buf, out1.dsize());
    kv2.deSerialize( in1 );
    EXPECT_EQ(kv1.k, kv2.k);
    EXPECT_EQ(kv1.v.valueLength, kv2.v.valueLength);
    EXPECT_EQ(kv1.v.meta.pStamp, kv2.v.meta.pStamp);
    EXPECT_EQ(kv1.v.isTombstone, kv2.v.isTombstone);
    EXPECT_EQ(strncmp((const char*)kv1.v.valuePtr, (const char*)kv2.v.valuePtr, kv1.v.valueLength), 0);

    // TxEntry
    TxEntry tx1(10, 10), tx2(1, 1);
    KVLayout kv10(16), kv11(16), kv12(16), kv13(16), kv14(16), kv15(16), kv16(16);
    tx1.writeSetIndex = 7;
    KVLayout **writeSet = tx1.getWriteSet().get();
    writeSet[0] = &kv10;
    writeSet[1] = &kv11;
    writeSet[2] = &kv12;
    writeSet[3] = &kv13;
    writeSet[4] = &kv14;
    writeSet[5] = &kv15;
    writeSet[6] = &kv16;
    tx1.txState = TxEntry::TX_PENDING;
    tx1.commitIntentState = TxEntry::TX_CI_INPROGRESS;

    outMemStream out2(buf, sizeof(buf));
    tx1.serialize( out2 );

    inMemStream in2(buf, out2.dsize());
    tx2.deSerialize ( in2 );

    EXPECT_EQ(tx1.getTxState(), tx2.getTxState());
    EXPECT_EQ(tx1.getTxCIState(), tx2.getTxCIState());

    KVLayout **writeSet1 = tx1.getWriteSet().get();
    KVLayout **writeSet2 = tx2.getWriteSet().get();

    for(uint32_t idx = 0; idx < tx2.getWriteSetIndex(); idx++) {
        KVLayout *kv1 = writeSet1[idx],
                 *kv2 = writeSet2[idx];
        EXPECT_EQ(kv1->k, kv2->k);
        EXPECT_EQ(kv1->v.valueLength, kv2->v.valueLength);
        EXPECT_EQ(kv1->v.meta.pStamp, kv2->v.meta.pStamp);
        EXPECT_EQ(kv1->v.isTombstone, kv2->v.isTombstone);
        EXPECT_EQ(strncmp((const char*)kv1->v.valuePtr, (const char*)kv2->v.valuePtr, kv1->v.valueLength), 0);
    }

}

}  // namespace RAMCloud
