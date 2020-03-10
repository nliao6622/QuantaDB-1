/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 */

#include "TestUtil.h"
#include "MockCluster.h"
#include "LeaseCommon.h"
#include "Validator.h"
#include "Tub.h"
#include "MultiWrite.h"
#include "Cycles.h"

#include <ostream>
#include <string>
#define GTEST_COUT  std::cerr << "[ INFO ] "

namespace RAMCloud {

using namespace DSSN;

class ValidatorTest : public ::testing::Test {
  public:
    TestLog::Enable logEnabler;
    Context context;
    MockCluster cluster;
    ClusterClock clusterClock;
    DSSN::Validator validator;
    TxEntry *txEntry[1000000];
    uint8_t dataBlob[512];

    ValidatorTest()
        : logEnabler()
        , context()
        , cluster(&context)
        , clusterClock()
    {
    }

    DISALLOW_COPY_AND_ASSIGN(ValidatorTest);

    void fillTxEntry(int noEntries, int noKeys = 1, int keySize = 32) {
        for (int i = 0; i < noEntries; i++) {
        	uint32_t rr = 0, ww = 0;
        	txEntry[i] = new TxEntry(4 * noKeys / 5, (noKeys + 4) / 5);
        	txEntry[i]->setCTS(i+1);
        	for (int j = 0; j < noKeys; j++) {
                KVLayout kv(keySize);
                snprintf((char *)kv.k.key.get(), keySize, "%dhajfk78uj3kjciu3jj9jij39u9j93j", j);
                kv.k.keyLength = keySize;
                kv.v.valuePtr = (uint8_t *)dataBlob;
                kv.v.valueLength = sizeof(dataBlob);
                KVLayout *kvOut = validator.kvStore.preput(kv);
        		if (j % 5 == 0)
        			txEntry[i]->insertWriteSet(kvOut, rr++);
        		else
        			txEntry[i]->insertReadSet(kvOut, ww++);
        	}
        }
    }

    void freeTxEntry(int noEntries) {
        for (int i = 0; i < noEntries; i++) {
        	delete txEntry[i];
        	txEntry[i] = 0;
        }
    }

    void printTxEntry(int noEntries) {
        for (int i = 0; i < noEntries; i++) {
        	if (txEntry[i] == 0)
        		break;
        	for (uint32_t j = 0; j < txEntry[i]->getReadSetSize(); j++) {
        		std::string str((char *)txEntry[i]->readSet[j]->k.key.get());
        		GTEST_COUT << "read key: " << str  << std::endl;
        	}
        	for (uint32_t j = 0; j < txEntry[i]->getWriteSetSize(); j++) {
        		std::string str((char *)txEntry[i]->writeSet[j]->k.key.get());
        	    GTEST_COUT << "write key: " << str  << std::endl;
        	}
        }
    }

    void printTxEntryCommits(int noEntries) {
    	int count = 0;
    	for (int i = 0; i < noEntries; i++) {
    		if (txEntry[i]->txState == TxEntry::TX_COMMIT)
    			count++;
    	}
    	GTEST_COUT << "Total commits: " << count << std::endl;
    }
};

TEST_F(ValidatorTest, BATKVStorePutGet) {
	fillTxEntry(1);

	for (uint32_t i = 0; i < txEntry[0]->getWriteSetSize(); i++) {
		uint8_t *valuePtr = 0;
		uint32_t valueLength;
		validator.kvStore.getValue(txEntry[0]->getWriteSet()[i]->k, valuePtr, valueLength);
		EXPECT_EQ(0, valuePtr);
		EXPECT_EQ(0, (int)valueLength);
		validator.kvStore.putNew(txEntry[0]->getWriteSet()[i], 0, 0);
		validator.kvStore.getValue(txEntry[0]->getWriteSet()[i]->k, valuePtr, valueLength);
	    EXPECT_NE(dataBlob, valuePtr);
	    EXPECT_EQ(sizeof(dataBlob), valueLength);
	    EXPECT_EQ(0, std::memcmp(dataBlob, valuePtr, valueLength));
	}

	freeTxEntry(1);
}

TEST_F(ValidatorTest, BATKVStorePutPerf) {
    uint64_t start, stop;

	fillTxEntry(1,1000000);

    uint32_t size = txEntry[0]->getWriteSetSize();
    auto& writeSet = txEntry[0]->getWriteSet();
    start = Cycles::rdtscp();
	for (uint32_t i = 0; i < size; i++) {
		validator.kvStore.putNew(writeSet[i], 0, 0);
	}
    stop = Cycles::rdtscp();
    GTEST_COUT << "write (" << size << " keys): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per write: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;
    //printTxEntry(1);

	freeTxEntry(1);
}

TEST_F(ValidatorTest, BATKVStorePutGetMulti) {
	fillTxEntry(5, 10);
	for (uint32_t i = 0; i < txEntry[0]->getWriteSetSize(); i++) {
		validator.kvStore.putNew(txEntry[0]->getWriteSet()[i], 0, 0);
		uint8_t *valuePtr = 0;
		uint32_t valueLength;
		validator.kvStore.getValue(txEntry[0]->getWriteSet()[i]->k, valuePtr, valueLength);
	    EXPECT_EQ(sizeof(dataBlob), valueLength);
	    EXPECT_EQ(0, std::memcmp(dataBlob, valuePtr, valueLength));
	}
	freeTxEntry(5);
}

TEST_F(ValidatorTest, BATValidateLocalTx) {
	// this tests the correctness of local tx validation

	uint8_t *valuePtr = 0;
	uint32_t valueLength;

    fillTxEntry(1);
    KLayout k(txEntry[0]->getWriteSet()[0]->k.keyLength);
    std::memcpy(k.key.get(), txEntry[0]->getWriteSet()[0]->k.key.get(), k.keyLength);

    validator.localTxQueue.push(txEntry[0]);
    validator.localTxQueue.schedule(true);
    validator.isUnderTest = true; //so that serialize loop will end when queue is empty
    validator.serialize();
    EXPECT_EQ(3, (int)txEntry[0]->txState); //COMMIT
	validator.kvStore.getValue(k, valuePtr, valueLength);
    EXPECT_EQ(sizeof(dataBlob), valueLength);
    EXPECT_EQ(0, std::memcmp(dataBlob, valuePtr, valueLength));

    freeTxEntry(1);

    fillTxEntry(1, 4); //one write key, three read keys

    validator.localTxQueue.push(txEntry[0]);
    validator.localTxQueue.schedule(true);
    validator.isUnderTest = true; //so that serialize loop will end when queue is empty
    validator.serialize();
    EXPECT_EQ(3, (int)txEntry[0]->txState); //COMMIT
	validator.kvStore.getValue(k, valuePtr, valueLength);
    EXPECT_EQ(sizeof(dataBlob), valueLength);
    EXPECT_EQ(0, std::memcmp(dataBlob, valuePtr, valueLength));

    freeTxEntry(1);
}

TEST_F(ValidatorTest, BATValidateLocalTxPerf) {
	// this tests performance of local tx validation

    int size = 10000;//(int)(sizeof(txEntry) / sizeof(TxEntry *));
    int count = 0;
    uint64_t start, stop;

    fillTxEntry(size, 10);

    GTEST_COUT << "WriteSet size " << txEntry[0]->getWriteSetSize() << std::endl;
    GTEST_COUT << "ReadSet size " << txEntry[0]->getReadSetSize() << std::endl;

    count = 0;
    start = Cycles::rdtscp();
    for (int i = 0; i < size; i++) {
    	if (validator.localTxQueue.push(txEntry[i])) count++;
    }
    validator.localTxQueue.schedule(true);
    stop = Cycles::rdtscp();
    GTEST_COUT << "localTxQueue.push(): Total cycles (" << size << " txs): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per local tx: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;
    EXPECT_EQ(size, count);

    //time pop()
    count = 0;
    start = Cycles::rdtscp();
    for (int i = 0; i < size; i++) {
    	TxEntry *tmp;
    	if (validator.localTxQueue.try_pop(tmp)) count++;
    }
    stop = Cycles::rdtscp();
    GTEST_COUT << "localTxQueue.try_pop(): Total cycles (" << size << " txs): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per local tx: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;
    EXPECT_EQ(size, count);

    //time blocks()
    start = Cycles::rdtscp();
    for (int i = 0; i < size; i++) {
    	if (validator.activeTxSet.blocks(txEntry[i])) {
    		EXPECT_EQ(0, 1);
    	}
    }
    stop = Cycles::rdtscp();
    GTEST_COUT << "activeTxSet.blocks(): Total cycles (" << size << " txs): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per local tx: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;

    //time validate()
    start = Cycles::rdtscp();
    for (int i = 0; i < size; i++) {
    	validator.validateLocalTx(*txEntry[i]);
    }
    stop = Cycles::rdtscp();
    GTEST_COUT << "validateLocalTx(): Total cycles (" << size << " txs): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per local tx: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;

    // time conclude()
    start = Cycles::rdtscp();
    for (int i = 0; i < size; i++) {
    	validator.conclude(*txEntry[i]);
    }
    stop = Cycles::rdtscp();
    GTEST_COUT << "conclude(): Total cycles (" << size << " txs): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per local tx: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;

    printTxEntryCommits(size);

    freeTxEntry(size);
}

TEST_F(ValidatorTest, BATValidateLocalTxPerf2) {
    int size = (int)(sizeof(txEntry) / sizeof(TxEntry *));
    uint64_t start, stop;

    fillTxEntry(size, 2);

    //printTxEntry(1);
    GTEST_COUT << "WriteSet size " << txEntry[0]->getWriteSetSize() << std::endl;
    GTEST_COUT << "ReadSet size " << txEntry[0]->getReadSetSize() << std::endl;

    //time all operations
    for (int i = 0; i < size; i++) {
    	validator.localTxQueue.push(txEntry[i]);
    }
    validator.localTxQueue.schedule(true);

    //uint64_t lastCTS = 1234;
    start = Cycles::rdtscp();
    for (int i = 0; i < size; i++) {
    	TxEntry *tmp;
    	validator.localTxQueue.try_pop(tmp);
    	validator.activeTxSet.blocks(tmp);
    	//tmp->setCTS(++lastCTS);
    	validator.validateLocalTx(*tmp);
    	validator.conclude(*tmp);
    }
    stop = Cycles::rdtscp();
    GTEST_COUT << "pop,blocks,validate,conclude: Total cycles (" << size << " txs): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per local tx: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;

    printTxEntryCommits(size);

    freeTxEntry(size);
}

TEST_F(ValidatorTest, BATValidateLocalTxs) {
    int size = (int)(sizeof(txEntry) / sizeof(TxEntry *));
    uint64_t start, stop;

    fillTxEntry(size, 10);

    GTEST_COUT << "WriteSet size " << txEntry[0]->getWriteSetSize() << std::endl;
    GTEST_COUT << "ReadSet size " << txEntry[0]->getReadSetSize() << std::endl;

    //time serializa()
    for (int i = 0; i < size; i++) {
    	validator.localTxQueue.push(txEntry[i]);
    }
    validator.localTxQueue.schedule(true);
    validator.isUnderTest = true; //so that serialize loop will end when queue is empty
    start = Cycles::rdtscp();
    validator.serialize();
    stop = Cycles::rdtscp();
    GTEST_COUT << "Serialize local txs: Total cycles (" << size << " txs): " << (stop - start) << std::endl;
    GTEST_COUT << "Sec per local tx: " << (Cycles::toSeconds(stop - start) / size)  << std::endl;

    printTxEntryCommits(size);

    freeTxEntry(size);
}

}  // namespace RAMCloud
