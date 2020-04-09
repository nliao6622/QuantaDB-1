/* Copyright (c) 2020  Futurewei Technologies, Inc.
 *
 * All rights are reserved.
 */
#include <atomic>
#include <queue>
#include <errno.h>
#include <time.h>
#include "HashmapKVStore.h"

namespace DSSN {

void *clhash_random;
bool HashmapKVStore::hash_inited = 0;

KVLayout* HashmapKVStore::preput(KVLayout &kvIn)
{
    KVLayout* kvOut = new KVLayout(kvIn.k.keyLength);
    std::memcpy((void *)kvOut->k.key.get(), (void *)kvIn.k.key.get(), kvOut->k.keyLength);
    kvOut->v.valueLength = kvIn.v.valueLength;
    kvOut->v.valuePtr = new uint8_t[kvIn.v.valueLength];
    std::memcpy((void *)kvOut->v.valuePtr, (void *)kvIn.v.valuePtr, kvIn.v.valueLength);
    return kvOut;
}

bool HashmapKVStore::putNew(KVLayout *kv, uint64_t cts, uint64_t pi)
{
    kv->getMeta().cStamp = kv->getMeta().pStamp = cts;
    kv->getMeta().pStampPrev = 0;
    kv->getMeta().sStampPrev = pi;
    kv->getMeta().sStamp = cts; //Fixme: tx pi or tx cts? the SSN paper is vague about this
    elem_pointer<KVLayout> lptr = my_hashtable->put(kv->getKey(), kv);
    return lptr.ptr_ != NULL;
}

bool HashmapKVStore::put(KVLayout *kv, uint64_t cts, uint64_t pi, uint8_t *valuePtr, uint32_t valueLength)
{
    kv->getMeta().cStamp = kv->getMeta().pStamp = cts;
    kv->getMeta().pStampPrev = kv->getMeta().pStamp;
    kv->getMeta().sStampPrev = pi;
    kv->getMeta().sStamp = cts; //Fixme: tx pi or tx cts? the SSN paper is vague about this
    if (kv->v.valuePtr)
        delete kv->v.valuePtr;
    kv->v.valueLength = valueLength;
    kv->v.valuePtr = valuePtr;
    return true;
}

KVLayout * HashmapKVStore::fetch(KLayout& k)
{

    elem_pointer<KVLayout> lptr = my_hashtable->get(k);
    return lptr.ptr_;
}

bool HashmapKVStore::getValue(KLayout& k, uint8_t *&valuePtr, uint32_t &valueLength)
{
    KVLayout * kv = fetch(k);

    if (kv == NULL) {
	    valueLength = 0;
        return false;
    }
	valuePtr = kv->v.valuePtr;
	valueLength = kv->v.valueLength;
	return true;
}

bool HashmapKVStore::getValue(KLayout& k, KVLayout *&kv)
{
    kv = fetch(k);
    return (kv != NULL);
}

bool HashmapKVStore::getMeta(KLayout& k, DSSNMeta &meta)
{
    KVLayout * kv = fetch(k);
    if (kv) {
	    meta = kv->getMeta();
		return true;
	}
	return false;
}

bool HashmapKVStore::maximizeMetaEta(KVLayout *kv, uint64_t eta) {
	kv->getMeta().pStamp = std::max(eta, kv->getMeta().pStamp);;
	return true;
}

bool HashmapKVStore::remove(KLayout& k, DSSNMeta &meta)
{
    KVLayout * kv = fetch(k);
    if (kv) {
		kv->isTombstone() = true;
		kv->getMeta() = meta;
		return true;
	}
	return false;
}

} // DSSN
