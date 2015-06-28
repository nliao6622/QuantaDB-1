/* Copyright (c) 2013-2014 Stanford University
 * Copyright (c) 2015 Diego Ongaro
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if ENABLE_LOGCABIN

#include "Common.h"
#include "Cycles.h"
#include "LogCabinStorage.h"
#include "StringUtil.h"

namespace RAMCloud {

typedef LogCabin::Client::Result LCResult;
typedef LogCabin::Client::Status LCStatus;

///////// LogCabinStorage public //////////

/**
 * Construct a LogCabinStorage object.
 *
 * \param serverInfo
 *      Describes where the LogCabin servers are running: see
 *      LogCabin::Client::Cluster constructor (comma-separated hostnames, which
 *      each may map to multiple addresses).
 */
LogCabinStorage::LogCabinStorage(const std::string& serverInfo)
    : mockableUsleep(usleep)
    , checkLeaderIntervalMs(1000)
    , renewLeaseIntervalMs(500)
    , expireLeaseIntervalMs(750)
    , exitingMutex()
    , isExiting(false)
    , exiting()
    , cluster(serverInfo)
    , tree(cluster.getTree())
    , keepAliveKey()
    , lastTimeoutNs(0)
    , leaseRenewer()
{
}

/**
 * Construct a LogCabinStorage object using an existing
 * LogCabin::Client::Cluster object. This is used for unit testing.
 * \param cluster
 *      An existing Cluster object, usually mocked out in the LogCabin client
 *      library for testing purposes.
 */
LogCabinStorage::LogCabinStorage(LogCabin::Client::Cluster cluster)
    : mockableUsleep(usleep)
    , checkLeaderIntervalMs(1000)
    , renewLeaseIntervalMs(500)
    , expireLeaseIntervalMs(750)
    , exitingMutex()
    , isExiting(false)
    , exiting()
    , cluster(cluster)
    , tree(cluster.getTree())
    , keepAliveKey()
    , lastTimeoutNs(0)
    , leaseRenewer()
{
}

/**
 * Destructor for LogCabinStorage objects.
 */
LogCabinStorage::~LogCabinStorage()
{
    {
        std::unique_lock<std::mutex> lockGuard(exitingMutex);
        isExiting = true;
        exiting.notify_all();
    }
    if (leaseRenewer.joinable())
        leaseRenewer.join();
}


// See documentation for ExternalStorage::becomeLeader.
void
LogCabinStorage::becomeLeader(const char* name, const std::string& leaderInfo)
{
    const std::string ownerKey = name;
    keepAliveKey = ownerKey + "-keepalive"; // class member

    LCResult result;
    std::string contents;
    std::string newContents;

  top:
    // Read current value of ownerKey into condition for all future operations
    // in this function (if ownerKey changes after this, the code will jump
    // back here to 'top').
    tree.setCondition("", "");
    contents.clear();
    result = tree.read(ownerKey, contents);
    switch (result.status) {
        case LCStatus::OK: {
            tree.setCondition(ownerKey, contents);
            break;
        }
        case LCStatus::LOOKUP_ERROR: {
            tree.setCondition(ownerKey, "");
            goto takeover;
        }
        default: {
            RAMCLOUD_DIE("Error reading %s: %s",
                         ownerKey.c_str(),
                         result.error.c_str());
        }
    }

    // read current value of keepAlive key into 'contents'
    contents.clear();
    result = tree.read(keepAliveKey, contents);
    switch (result.status) {
        case LCStatus::OK: // fallthrough
        case LCStatus::LOOKUP_ERROR: {
           break;
        }
        case LCStatus::CONDITION_NOT_MET: {
            goto top; // ownerKey changed, start over
        }
        default: {
            RAMCLOUD_DIE("Error reading %s: %s",
                         keepAliveKey.c_str(),
                         result.error.c_str());
        }
    }

  sleep:
    // wait the required period of time
    mockableUsleep(downCast<unsigned int>(checkLeaderIntervalMs * 1000));

    // read value of keep-alive key again
    newContents.clear();
    result = tree.read(keepAliveKey, newContents);
    switch (result.status) {
        case LCStatus::OK: // fallthrough
        case LCStatus::LOOKUP_ERROR: {
            if (newContents != contents) {
                // changed, leader was alive, sleep again
                contents = newContents;
                goto sleep;
            } else {
                // different: we get to break the lease
                goto takeover;
            }
        }
        case LCStatus::CONDITION_NOT_MET: {
            goto top; // ownerKey changed, start over
        }
        default: {
            RAMCLOUD_DIE("Error reading %s: %s",
                         keepAliveKey.c_str(),
                         result.error.c_str());
        }
    }

  takeover:
    // try to overwrite the lease
    TimePoint start = Clock::now();
    std::string leaderInfoWithNonce =
        format("%016lx:", generateRandom()) + leaderInfo;
    result = tree.write(ownerKey, leaderInfoWithNonce);
    switch (result.status) {
        case LCStatus::OK: {
            tree.setCondition(ownerKey, leaderInfoWithNonce);
            if (renewLeaseIntervalMs != 0) {
                leaseRenewer = std::thread(&LogCabinStorage::leaseRenewerMain,
                                           this,
                                           start);
            }
            return;
        }
        case LCStatus::LOOKUP_ERROR: {
            makeParents(ownerKey.c_str());
            goto takeover; // try again
        }
        case LCStatus::CONDITION_NOT_MET: {
            goto top; // ownerKey changed, start over
        }
        default: {
            RAMCLOUD_DIE("Error writing %s: %s",
                         ownerKey.c_str(),
                         result.error.c_str());
        }
    }
}

// See documentation for ExternalStorage::get.
bool
LogCabinStorage::get(const char* name, Buffer* value)
{
    value->reset();
    std::string contents;
    LCResult result = tree.read(name, contents);
    switch (result.status) {
        case LCStatus::OK: {
            value->appendCopy(contents.data(),
                              downCast<uint32_t>(contents.size()));
            return true;
        }
        case LCStatus::TYPE_ERROR: {
            // 'name' is probably a directory: return true with empty value.
            // It's also possible that a parent of 'name' is a file, but that's
            // hard to distinguish here and probably not worth any trouble.
            return true;
        }
        case LCStatus::LOOKUP_ERROR: {
            return false;
        }
        case LCStatus::CONDITION_NOT_MET: {
            RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                         result.error.c_str());
            throw LostLeadershipException(HERE);
        }
        default: {
            RAMCLOUD_DIE("Error reading %s: %s",
                         name,
                         result.error.c_str());
        }
    }
}

// See documentation for ExternalStorage::getChildren.
void
LogCabinStorage::getChildren(const char* name,
                             std::vector<ExternalStorage::Object>* children)
{
    children->clear();

    // Fetch names of children.
    std::vector<std::string> childNames;
    LCResult result = tree.listDirectory(name, childNames);
    switch (result.status) {
        case LCStatus::OK: {
            break;
        }
        case LCStatus::LOOKUP_ERROR: {
            return;
        }
        case LCStatus::CONDITION_NOT_MET: {
            RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                         result.error.c_str());
            throw LostLeadershipException(HERE);
        }
        default: {
            RAMCLOUD_DIE("Error listing %s: %s",
                         name,
                         result.error.c_str());
        }
    }

    // Read each child.
    const std::string prefix = std::string(name) + "/";
    foreach (const std::string& childName, childNames) {
        std::string childPath = prefix + childName;
        if (StringUtil::endsWith(childName, "/")) {
            // child is a directory
            childPath.erase(childPath.length() - 1); // strip trailing slash
            children->emplace_back(childPath.c_str(),
                                   static_cast<const char*>(NULL), 0);
        } else {
            // child is a file
            std::string contents;
            LCResult result = tree.read(childPath, contents);
            switch (result.status) {
                case LCStatus::OK: {
                    children->emplace_back(childPath.c_str(),
                                           contents.data(),
                                           contents.length());
                    break;
                }
                case LCStatus::LOOKUP_ERROR: {
                    // deleted in the meantime: skip it
                    break;
                }
                case LCStatus::CONDITION_NOT_MET: {
                    RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                                 result.error.c_str());
                    throw LostLeadershipException(HERE);
                }
                default: {
                    RAMCLOUD_DIE("Error reading %s: %s",
                                 childPath.c_str(),
                                 result.error.c_str());
                }
            }
        }
    }
}

// See documentation for ExternalStorage::getLeaderInfo.
bool
LogCabinStorage::getLeaderInfo(const char* name, Buffer* value)
{
    bool ret = get(name, value);
    // strip first 17 bytes (16 hexadecimal digits plus colon)
    if (value->size() >= 17) {
        value->truncateFront(17);
    }
    return ret;
}

// See documentation for ExternalStorage::getWorkspace.
const char*
LogCabinStorage::getWorkspace()
{
    return ExternalStorage::getWorkspace();
    // It'd be nicer to:
    // return tree.getWorkingDirectory();
    // but the lifetime of the return value is broken.
}

// See documentation for ExternalStorage::remove.
void
LogCabinStorage::remove(const char* name)
{
    // LogCabin only has typed forms of remove: removeDirectory and
    // removeFile, so we have to pick one to try first. Removing a file is
    // probably more common, so try that first.
    LCResult result = tree.removeFile(name);
    switch (result.status) {
        case LCStatus::OK: // fallthrough
        case LCStatus::LOOKUP_ERROR: {
            return;
        }
        case LCStatus::CONDITION_NOT_MET: {
            RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                         result.error.c_str());
            throw LostLeadershipException(HERE);
        }
        case LCStatus::TYPE_ERROR: {
            // it's probably a directory, try removeDirectory
            break;
        }
        default: {
            RAMCLOUD_DIE("Error removing file %s: %s",
                         name,
                         result.error.c_str());
        }
    }

    result = tree.removeDirectory(name);
    switch (result.status) {
        case LCStatus::OK: // fallthrough
        case LCStatus::LOOKUP_ERROR: {
            return;
        }
        case LCStatus::CONDITION_NOT_MET: {
            RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                         result.error.c_str());
            throw LostLeadershipException(HERE);
        }
        case LCStatus::TYPE_ERROR: // fallthrough (tried both ways now)
        default: {
            RAMCLOUD_DIE("Error removing directory %s: %s",
                         name,
                         result.error.c_str());
        }
    }
}

// See documentation for ExternalStorage::update.
void
LogCabinStorage::set(Hint flavor, const char* name, const char* value,
                     int valueLength)
{
    std::string contents;
    if (valueLength < 0)
        contents.assign(value);
    else
        contents.assign(value, size_t(valueLength));

    while (true) {
        LCResult result = tree.write(name, contents);
        switch (result.status) {
            case LCStatus::OK: {
                return;
            }
            case LCStatus::LOOKUP_ERROR: {
                makeParents(name);
                break; // try again
            }
            case LCStatus::CONDITION_NOT_MET: {
                RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                             result.error.c_str());
                throw LostLeadershipException(HERE);
            }
            default: {
                RAMCLOUD_DIE("Error writing %s: %s",
                             name,
                             result.error.c_str());
            }
        }
    }
}

// See documentation for ExternalStorage::setWorkspace.
void
LogCabinStorage::setWorkspace(const char* pathPrefix)
{
    // Call into base class so that getWorkspace will function.
    ExternalStorage::setWorkspace(pathPrefix);

    LCResult result = tree.setWorkingDirectory(pathPrefix);
    switch (result.status) {
        case LCStatus::OK: {
            return;
        }
        case LCStatus::CONDITION_NOT_MET: {
            RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                         result.error.c_str());
            throw LostLeadershipException(HERE);
        }
        default: {
            RAMCLOUD_DIE("Error setting working directory to %s: %s",
                         pathPrefix,
                         result.error.c_str());
        }
    }
}

///////// LogCabinStorage private //////////

/**
 * Main function for #leaseRenewer thread.
 * Periodically invokes renewLease() after each renewLeaseIntervalMs.
 * \param start
 *      The time just before the lease was initially established.
 */
void
LogCabinStorage::leaseRenewerMain(TimePoint start)
{
    std::unique_lock<std::mutex> lockGuard(exitingMutex);
    using std::chrono::milliseconds;
    while (!isExiting) {
        TimePoint renewAt = start + milliseconds(renewLeaseIntervalMs);
        if (Clock::now() < renewAt) {
            exiting.wait_until(lockGuard, renewAt);
            continue;
        }
        TimePoint deadline = start + milliseconds(expireLeaseIntervalMs);
        start = Clock::now();
        renewLease(deadline);
    }
}

/**
 * Recursively create parents of 'name'.
 */
void
LogCabinStorage::makeParents(const char* name)
{
    LCResult result = tree.makeDirectory(std::string(name) + "/..");
    switch (result.status) {
        case LCStatus::OK: {
            return;
        }
        case LCStatus::CONDITION_NOT_MET: {
            RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                         result.error.c_str());
            throw LostLeadershipException(HERE);
        }
        default: {
            RAMCLOUD_DIE("Error creating parents of %s: %s",
                         name,
                         result.error.c_str());
        }
    }
}


/**
 * This method is invoked when the leaseRenewer wakes up; its job is
 * to update the leader keepalive object in order to renew our lease as leader.
 * \param deadline
 *      If the lease can't be renewed by this time, crash the process.
 */
void
LogCabinStorage::renewLease(TimePoint deadline)
{
    // the exact contents doesn't matter but needs to be different each time
    std::string contents = format("%lu", Cycles::rdtsc());
    LogCabin::Client::Tree treeWithTimeout = tree;
    TimePoint now = Clock::now();
    if (deadline <= now) {
        // The deadline is past: using a timeout of 1 nanosecond will return
        // control pretty quickly (using 0 would mean no timeout at all).
        lastTimeoutNs = 1;
    } else {
        lastTimeoutNs = std::chrono::nanoseconds(deadline - now).count();
    }
    treeWithTimeout.setTimeout(lastTimeoutNs);
    LCResult result = treeWithTimeout.write(keepAliveKey, contents);
    switch (result.status) {
        case LCStatus::OK: {
            return;
        }
        case LCStatus::TIMEOUT: // fallthrough
        case LCStatus::CONDITION_NOT_MET: {
            RAMCLOUD_LOG(WARNING, "Lost LogCabin leadership: %s",
                         result.error.c_str());
            // there's no stack frame above to catch this, but let's use the
            // exception anyway since the rest of the file does
            throw LostLeadershipException(HERE);
        }
        default: {
            RAMCLOUD_DIE("Error writing %s: %s",
                         keepAliveKey.c_str(),
                         result.error.c_str());
        }
    }
}

} // namespace RAMCloud

#endif // ENABLE_LOGCABIN
